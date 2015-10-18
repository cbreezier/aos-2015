#include <nfs/nfs.h>
#include <utils/page.h>
#include <sync/mutex.h>
#include <sys/panic.h>
#include <sys/debug.h>
#include <string.h>
#include <sys/stat.h>
#include <utils/page.h>
#include "swap.h"
#include "frametable.h"
#include "pagetable.h"
#include "nfs_sync.h"
#include "alloc_wrappers.h"


/* 
 * Bounds of the frametable. _cur_ft_idx denotes the current position
 * of the clock algorithm
 */
size_t _lo_ft_idx, _hi_ft_idx, _cur_ft_idx;
/* File handle to the swap file */
fhandle_t swap_fh;

/* Swap file free entry list */
int swap_free_head, swap_free_tail;

void swap_init(size_t lo_ft_idx, size_t hi_ft_idx) {
    _lo_ft_idx = lo_ft_idx;
    _hi_ft_idx = hi_ft_idx;
    _cur_ft_idx = lo_ft_idx;

    swap_table = (struct swap_entry *)frame_alloc_sos(true);

    void *prev = (void *)swap_table;
    conditional_panic(!swap_table, "Unable to allocate frame 0 of swap table");

    size_t num_entries = (hi_ft_idx - lo_ft_idx) * SWAP_MULTIPLIER;
    size_t mem_required = num_entries * sizeof(struct swap_entry);
    size_t frames_required = PAGE_ALIGN_UP(mem_required, PAGE_SIZE) / PAGE_SIZE;

    /* Allocate memory for free entry list. This memory must be contiguous */
    for (size_t i = 1; i < frames_required; ++i) {
        void *cur = (void *)frame_alloc_sos(true);
        conditional_panic(prev + PAGE_SIZE != cur, "Swap table frame not contiguous");
        prev += PAGE_SIZE;
    }
    int err = nfs_create_sync("swap", S_IRUSR | S_IWUSR, num_entries * PAGE_SIZE, &swap_fh, NULL);
    conditional_panic(err, "Cannot create swap file");

    swap_free_head = 1;
    swap_free_tail = num_entries - 1;
    /* Location 0 is invalid, as -0 also refers to an unallocated page */
    swap_table[0].next_free = -1;
    for (size_t i = 1; i < num_entries; ++i) {
        swap_table[i].next_free = (i == num_entries - 1) ? -1 : i + 1;
    }
}

static int swapout() {
    size_t num_looped_entries = 0;
    /*
     * Find an un-referenced page in the frametable.
     * Any looped over page is un-referenced, and unmapped,
     * such that we can know when it's re-used.
     */
    while (ft[_cur_ft_idx].referenced || !ft[_cur_ft_idx].is_swappable) {
        struct ft_entry *fte = &ft[_cur_ft_idx];

        if (fte->user_cap && fte->is_swappable) {
            fte->referenced = false;

            int err = seL4_ARM_Page_Unmap(fte->user_cap);
            conditional_panic(err, "Unable to unmap page (swapout)");

            /* Remove the capability itself */
            err = cspace_delete_cap(cur_cspace, fte->user_cap);
            conditional_panic(err, "unable to delete cap(swapout)");
            fte->user_cap = 0;
        }
        _cur_ft_idx++;
        if (_cur_ft_idx >= _hi_ft_idx) {
            _cur_ft_idx = _lo_ft_idx;
        }
        ++num_looped_entries;
        /* If we've looped over every entry twice, and still can't find one, simply panic */
        conditional_panic(num_looped_entries > (_hi_ft_idx - _lo_ft_idx + 1)*2 + 1, "All frames are allocated to SOS. No user frames left");
    }

    
    struct ft_entry *out_fte = &ft[_cur_ft_idx];

    if (swap_free_head == -1) {
        return -ENOMEM;
    }

    /* Update swap free list */
    int disk_loc = swap_free_head;
    swap_free_head = swap_table[swap_free_head].next_free;

    conditional_panic(!out_fte, "Invalid ft_entry (swap)");
    conditional_panic(!out_fte->vaddr_proc, "a ft_entry (swap)");
    conditional_panic(!out_fte->vaddr_proc->as, "b ft_entry (swap)");
    struct pt_entry *out_pte = vaddr_to_pt_entry(out_fte->vaddr_proc->as, out_fte->vaddr);
    conditional_panic(!out_pte, "Swapped out page is not in page table");
    conditional_panic(out_pte->frame != frame_idx_to_svaddr(_cur_ft_idx), "Page table and frametable are not synced");

    /* Indicate that the swapped out page is on disk, at a particular location */
    out_pte->frame = -disk_loc;

    seL4_Word svaddr = frame_idx_to_svaddr(_cur_ft_idx);
    /* Write the swapped out page to disk */
    int nwritten = nfs_sos_write_sync(swap_fh, disk_loc * PAGE_SIZE, (void*)svaddr, PAGE_SIZE);
    conditional_panic(nwritten != PAGE_SIZE, "Writing out to swap file failed");

    int ret = _cur_ft_idx++;
    if (_cur_ft_idx >= _hi_ft_idx) {
        _cur_ft_idx = _lo_ft_idx;
    }
    return ret;
}

int swapin(process_t *proc, seL4_Word vaddr, seL4_Word *ret_svaddr) {
    assert(ret_svaddr != NULL);
    conditional_panic(vaddr % PAGE_SIZE != 0, "vaddr provided to swapin is not page aligned");
    sync_acquire(ft_lock);  

    int frame_idx = -EFAULT;

    if (*ret_svaddr == 0) {
        frame_idx = swapout();
    } else {
        frame_idx = svaddr_to_frame_idx(*ret_svaddr);
    }
    conditional_panic(frame_idx == 0, "Invalid frame_idx/ret_svaddr (swapin)");
    /* We must have run out of swap file space */
    if (frame_idx < 0) {
        sync_release(ft_lock);
        return -frame_idx;
    }

    struct pt_entry *in_pte = vaddr_to_pt_entry(proc->as, vaddr);
    conditional_panic(!in_pte, "Page entry does not exist (swapin)");
    conditional_panic(in_pte->frame > 0, "Trying to swap in a page which is already swapped in");

    *ret_svaddr = frame_idx_to_svaddr(frame_idx);

    /* 
     * If the page we're trying to swap in is on disk, read it, and write
     * the contents to the frametable entry. Otherwise, simply zero
     * out the frame 
     */
    if (in_pte->frame < 0) {
        int disk_loc_in = -(in_pte->frame);
        /* Fetch page from disk */   
        int nread = nfs_sos_read_sync(swap_fh, disk_loc_in * PAGE_SIZE, (void*)(*ret_svaddr), PAGE_SIZE);
        conditional_panic(nread != PAGE_SIZE, "Reading in from swap file failed");

        /* Must unify, as we don't know if the type of the page */
        seL4_ARM_Page_Unify_Instruction(ft[frame_idx].cap, 0, PAGE_SIZE);

        free_swap_entry(disk_loc_in);
    } else {
        /* Equal to zero at this point. Brand new page, zero it out */
        memset((void*)(*ret_svaddr), 0, PAGE_SIZE);
    }
    
    sync_release(ft_lock);
    return 0;
}

int swapin_sos(seL4_Word *ret_svaddr) {
    assert(ret_svaddr != NULL);
    sync_acquire(ft_lock); 

    int frame_idx = swapout();
    if (frame_idx < 0) {
        sync_release(ft_lock);
        return ENOMEM;
    }

    *ret_svaddr = frame_idx_to_svaddr(frame_idx);

    /* Zero the frame and pin it */
    memset((void*)(*ret_svaddr), 0, PAGE_SIZE);
    frame_change_swappable(*ret_svaddr, false);

    sync_release(ft_lock);
    return 0;
}

void free_swap_entry(int entry_idx) {
    if (swap_free_head == -1 || swap_free_tail == -1) {
        /* No fre entries in the swap file */
        assert(swap_free_tail == -1 && swap_free_head == -1);
        swap_free_head = entry_idx;
        swap_free_tail = entry_idx;
        swap_table[entry_idx].next_free = -1;
    } else {
        /* Simply add to the end of the free list */
        swap_table[swap_free_tail].next_free = entry_idx;
        swap_table[entry_idx].next_free = -1;
        swap_free_tail = entry_idx;
    }
}
