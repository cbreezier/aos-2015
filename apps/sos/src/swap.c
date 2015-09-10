#include "swap.h"
#include "frametable.h"
#include "pagetable.h"
#include "nfs_sync.h"
#include <nfs/nfs.h>
#include <sync/mutex.h>
#include <sys/panic.h>
#include <string.h>

size_t _lo_ft_idx, _hi_ft_idx, _cur_ft_idx;
fhandle_t swap_fh;

int swap_free_head, swap_free_tail;

void swap_init(size_t lo_ft_idx, size_t hi_ft_idx) {
    _lo_ft_idx = lo_ft_idx;
    _hi_ft_idx = hi_ft_idx;
    _cur_ft_idx = lo_ft_idx;
    int err = nfs_create_sync("swap", FM_READ | FM_WRITE, SWAP_TABLE_SIZE * PAGE_SIZE, &swap_fh, NULL);
    conditional_panic(err, "Cannot create swap file");

    swap_free_head = 0;
    swap_free_tail = SWAP_TABLE_SIZE - 1;
    for (size_t i = 0; i < SWAP_TABLE_SIZE; ++i) {
        swap_table[i].next_free = (i == SWAP_TABLE_SIZE - 1) ? -1 : i + 1;
    }
}

static int swapout() {
    /*
     * Find an un-referenced page in the frametable.
     * Any looped over page is un-referenced, and unmapped
     */
    while (ft[_cur_ft_idx].referenced || !ft[_cur_ft_idx].is_swappable) {
        struct ft_entry *fte = &ft[_cur_ft_idx];

        if (fte->user_cap && !fte->is_swappable) {
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
    swap_free_head = swap_table[swap_free_head].next_free;

    struct pt_entry *out_pte = vaddr_to_pt_entry(out_fte->vaddr_proc->as, out_fte->vaddr);
    conditional_panic(!out_pte, "Swapped out page is not in page table");

    out_pte->frame = -disk_loc;

    seL4_Word svaddr = frame_idx_to_vaddr(_cur_ft_idx);
    int err = nfs_sos_write_sync(swap_fh, disk_loc * PAGE_SIZE, (void*)svaddr, PAGE_SIZE);
    if (err) {
        return -err;
    }

    return _cur_ft_idx;
}

int swapin(process_t *proc, seL4_Word vaddr, seL4_Word *svaddr) {
    sync_acquire(ft_lock);  

    int frame_idx = swapout();
    if (frame_idx < 0) {
        return -frame_idx;
    }

    struct pt_entry *in_pte = vaddr_to_pt_entry(proc->as, vaddr);
    if (in_pte == NULL) {
        return EFAULT;
    }
    conditional_panic(in_pte->frame > 0, "Trying to swap in a page which is already swapped in");

    *svaddr = frame_idx_to_vaddr(frame_idx);

    if (in_pte->frame < 0) {
        /* Fetch page from disk */   
        int err = nfs_sos_read_sync(swap_fh, -(in_pte->frame) * PAGE_SIZE, (void*)(*svaddr), PAGE_SIZE);
        if (err) {
            return err;
        }
    } else {
        /* Equal to zero at this point. Brand new page, zero it out */
        memset((void*)(*svaddr), 0, PAGE_SIZE);
    }
    
    sync_release(ft_lock);
    return 0;
}
