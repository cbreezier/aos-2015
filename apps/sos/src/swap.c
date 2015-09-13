#include "swap.h"
#include "frametable.h"
#include "pagetable.h"
#include "nfs_sync.h"
#include <nfs/nfs.h>
#include <sync/mutex.h>
#include <sys/panic.h>
#include <string.h>
#include <sys/stat.h>

size_t _lo_ft_idx, _hi_ft_idx, _cur_ft_idx;
fhandle_t swap_fh;

int swap_free_head, swap_free_tail;

void swap_init(size_t lo_ft_idx, size_t hi_ft_idx) {
    printf("%x %x\n", lo_ft_idx, hi_ft_idx);
    _lo_ft_idx = lo_ft_idx;
    _hi_ft_idx = hi_ft_idx;
    _cur_ft_idx = lo_ft_idx;

    swap_table = (struct swap_entry *)frame_alloc(1, 0);
    void *prev = (void *)swap_table;
    conditional_panic(!swap_table, "Unable to allocate frame 0 of swap table");
    size_t num_entries = (hi_ft_idx - lo_ft_idx) * SWAP_MULTIPLIER;
    size_t mem_required = num_entries * sizeof(struct swap_entry);
    /* frame_requires = ceil(mem_required / PAGE_SIZE) */
    size_t frames_required = (mem_required + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 1; i < frames_required; ++i) {
        void *cur = (void *)frame_alloc(1, 0);
        //printf("prev %x cur %x\n", prev, cur);
        conditional_panic(prev + PAGE_SIZE != cur, "Swap table frame not contiguous");
        prev += PAGE_SIZE;
    }
    int err = nfs_create_sync("swap", S_IRUSR | S_IWUSR, num_entries * PAGE_SIZE, &swap_fh, NULL);
    conditional_panic(err, "Cannot create swap file");

    swap_free_head = 0;
    swap_free_tail = num_entries - 1;
    for (size_t i = 0; i < num_entries; ++i) {
        swap_table[i].next_free = (i == num_entries - 1) ? -1 : i + 1;
    }
    printf("swap init done\n");
}

static int swapout() {
    /*
     * Find an un-referenced page in the frametable.
     * Any looped over page is un-referenced, and unmapped
     */
    while (ft[_cur_ft_idx].referenced || !ft[_cur_ft_idx].is_swappable) {
        // printf("considering frame %d referenced %d swappable %d\n", _cur_ft_idx, ft[_cur_ft_idx].referenced, ft[_cur_ft_idx].is_swappable);
        struct ft_entry *fte = &ft[_cur_ft_idx];

        if (fte->user_cap && fte->is_swappable) {
            fte->referenced = false;

            int err = seL4_ARM_Page_Unmap(fte->user_cap);
            conditional_panic(err, "Unable to unmap page (swapout)");

            /* Remove all child capabilities */
            err = cspace_revoke_cap(cur_cspace, fte->user_cap);
            conditional_panic(err, "unable to revoke cap(swapout)");

            /* Remove the capability itself */
            err = cspace_delete_cap(cur_cspace, fte->user_cap);
            conditional_panic(err, "unable to delete cap(swapout)");
            fte->user_cap = 0;
        }
        _cur_ft_idx++;
        if (_cur_ft_idx >= _hi_ft_idx) {
            _cur_ft_idx = _lo_ft_idx;
        }
    }

    
    struct ft_entry *out_fte = &ft[_cur_ft_idx];

    if (swap_free_head == -1) {
        return -ENOMEM;
    }

    /* Update swap free list */
    int disk_loc = swap_free_head;
    //printf("disc loc = %d\n", disk_loc);
    swap_free_head = swap_table[swap_free_head].next_free;

    //printf("vaddr in out ft idx %x\n", out_fte->vaddr);
    conditional_panic(!out_fte, "Invalid ft_entry (swap)");
    conditional_panic(!out_fte->vaddr_proc, "a ft_entry (swap)");
    conditional_panic(!out_fte->vaddr_proc->as, "b ft_entry (swap)");
    struct pt_entry *out_pte = vaddr_to_pt_entry(out_fte->vaddr_proc->as, out_fte->vaddr);
    conditional_panic(!out_pte, "Swapped out page is not in page table");
    conditional_panic(out_pte->frame != frame_idx_to_vaddr(_cur_ft_idx), "Page table and frametable are not synced");

    out_pte->frame = -disk_loc;

    seL4_Word svaddr = frame_idx_to_vaddr(_cur_ft_idx);
    //printf("writing at loc %d\n", disk_loc * PAGE_SIZE);
    int nwritten = nfs_sos_write_sync(swap_fh, disk_loc * PAGE_SIZE, (void*)svaddr, PAGE_SIZE);
    if (nwritten < 0) {
        return nwritten;
    }

    return _cur_ft_idx;
}

int swapin(process_t *proc, seL4_Word vaddr, seL4_Word *svaddr) {
    sync_acquire(ft_lock);  
    //printf("swapping in %x\n", vaddr);

    int frame_idx = swapout();
    if (frame_idx < 0) {
        printf("warning warning swapout frame_idx %d\n", frame_idx);
        return -frame_idx;
    }

    struct pt_entry *in_pte = vaddr_to_pt_entry(proc->as, vaddr);
    if (in_pte == NULL) {
        printf("warning warning null pt_entry\n");
        return EFAULT;
    }
    //printf("in_pte->frame = %d\n", in_pte->frame);
    conditional_panic(in_pte->frame > 0, "Trying to swap in a page which is already swapped in");

    *svaddr = frame_idx_to_vaddr(frame_idx);

    if (in_pte->frame < 0) {
        int disk_loc_in = -(in_pte->frame);
        /* Fetch page from disk */   
        int err = nfs_sos_read_sync(swap_fh, disk_loc_in * PAGE_SIZE, (void*)(*svaddr), PAGE_SIZE);
        if (err) {
            printf("warning warning nfs read sucks\n");
            return err;
        }
        swap_table[swap_free_tail].next_free = disk_loc_in;
        swap_table[disk_loc_in].next_free = -1;
        swap_free_tail = disk_loc_in;
    } else {
        /* Equal to zero at this point. Brand new page, zero it out */
        memset((void*)(*svaddr), 0, PAGE_SIZE);
    }
    
    sync_release(ft_lock);
    return 0;
}
