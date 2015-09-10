#include "swap.h"
#include "frametable.h"
#include "pagetable.h"
#include "nfs_sync.h"
#include <nfs/nfs.h>
#include <sync/mutex.h>
#include <sys/panic.h>

size_t _lo_ft_idx, _hi_ft_idx, _cur_ft_idx;
fhandle_t swap_fh;

int free_head, free_tail;

void swap_init(size_t lo_ft_idx, size_t hi_ft_idx) {
    _lo_ft_idx = lo_ft_idx;
    _hi_ft_idx = hi_ft_idx;
    _cur_ft_idx = lo_ft_idx;
    int err = nfs_create_sync("swap", FM_READ | FM_WRITE, SWAP_TABLE_SIZE * PAGE_SIZE, &swap_fh, NULL);
    conditional_panic(err, "Cannot create swap file");

    free_head = 0;
    free_tail = SWAP_TABLE_SIZE - 1;
    for (size_t i = 0; i < SWAP_TABLE_SIZE; ++i) {
        swap_table[i].next_free = (i == SWAP_TABLE_SIZE - 1) ? -1 : i + 1;
    }
}

static int swapout() {
    while (ft[_cur_ft_idx].referenced) {
        struct ft_entry *fte = &ft[_cur_ft_idx];

        fte->referenced = false;
        if (fte->user_cap) {
            int err = seL4_ARM_Page_Unmap(fte->user_cap);
            conditional_panic(err, "Unable to unmap page (swapout)");

            /* Remove all child capabilities */
            err = cspace_revoke_cap(cur_cspace, fte->user_cap);
            conditional_panic(err, "unable to revoke cap(swapout)");

            /* Remove the capability itself */
            err = cspace_delete_cap(cur_cspace, fte->user_cap);
            conditional_panic(err, "unable to delete cap(swapout)");
        }

    }
    return 0;
}

int swapin(process_t *proc, seL4_Word vaddr, seL4_Word *svaddr) {
//    sync_acquire(ft_lock);  
//
//    int frame_idx = swapout();
//
//
//    
//    sync_release(ft_lock);
    return 0;
}
