#include <stdlib.h>
#include <utils/page.h>
#include <frametable.h>
#include <ut_manager/ut.h>
#include <bits/limits.h>
#include <cspace/cspace.h>
#include <sys/panic.h>
#include <utils/mapping.h>
#include <bits/errno.h>
#include <string.h>
#include <sys/debug.h>
#include "swap.h"
#include "alloc_wrappers.h"

/*
 * This frametable uses a linked list approach for finding the next
 * free frame. Each free frame has an index field referring to the next
 * frame in the list.
 *
 * Note that '0', while a valid frame, is always allocated to the
 * frametable itself. Therefore, it cannot be given away to a user, and
 * internally we use it in a similar way to NULL.
 */


uint32_t free_head, free_tail;

seL4_Word low_addr, hi_addr, first_valid_frame, num_frames;


void frametable_init() {
    ut_find_memory(&low_addr, &hi_addr);
    //hi_addr -= PAGE_SIZE*128;
    hi_addr -= PAGE_SIZE + (1 << 23); //* 108 + (1 << 23);
    //hi_addr -= PAGE_SIZE * 512;
    dprintf(0, "low addr = %x, high = %x\n", low_addr, hi_addr);

    seL4_Word memory_size = hi_addr - low_addr;

    /* Figure out how much of the frametable is needed to store itself based on memory size */
    num_frames = PAGE_ALIGN_UP(memory_size, PAGE_SIZE) / PAGE_SIZE;
    seL4_Word frametable_size = num_frames * sizeof(struct ft_entry);
    seL4_Word frametable_frames_required = PAGE_ALIGN_UP(frametable_size, PAGE_SIZE) / PAGE_SIZE;

    ft = (struct ft_entry*) low_addr;
    free_head = frametable_frames_required;
    free_tail = num_frames-1;

    /* First valid frame past the frames pre-allocated for the frametable itself */
    first_valid_frame = frametable_frames_required;

    /* Allocate all frames required to hold the frametable data */
    for (uint32_t i = 0; i < frametable_frames_required; ++i) {
        seL4_Word id = kut_alloc(seL4_PageBits); 
        if (!id) {
            panic("Unable to init frametable(ut alloc)");
        }

        seL4_CPtr cap;
        int err = cspace_ut_retype_addr(id, seL4_ARM_SmallPageObject, seL4_PageBits, cur_cspace, &cap);
        conditional_panic(err, "Unable to init frametable(retype)");

        err = map_page(cap, seL4_CapInitThreadPD, low_addr + PAGE_SIZE*i, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        conditional_panic(err, "Unable to init frametable(map)");

        ft[i].paddr = id;
        ft[i].cap = cap;
        ft[i].user_cap = 0;
        ft[i].next_free = 0;
        ft[i].is_freeable = 0;
        ft[i].is_swappable = 0;
    }

    /* Default initialise all frames which can be given away to users */
    for (uint32_t i = frametable_frames_required; i < num_frames; ++i) {
        ft[i].paddr = 0;
        ft[i].cap = 0;
        ft[i].user_cap = 0;

        ft[i].next_free = (i == num_frames - 1) ? 0 : i+1;
        ft[i].is_freeable = 1;
        ft[i].is_swappable = 0;
    }
    ft_lock = sync_create_mutex();
    conditional_panic(!ft_lock, "Unable to create ft lock");
}

seL4_Word frame_alloc(bool freeable, bool swappable) { 
    sync_acquire(ft_lock);

    /* No free frames left */
    if (free_head == 0) {
        /* Paging stuff - return 0 and swapping is handled elsewhere */
        sync_release(ft_lock);
        return 0;
    }
    int idx = free_head;
    
    /* Not enough memory to allocate another frame */
    ft[idx].paddr = kut_alloc(seL4_PageBits);
    if (!ft[idx].paddr) {
        /* Paging stuff - return 0 and swapping is handled elsewhere */
        sync_release(ft_lock);
        return 0;
    }

    /* Figure out svaddr based on frame index */
    seL4_Word svaddr = low_addr + PAGE_SIZE*idx;

    int err = cspace_ut_retype_addr(ft[idx].paddr, seL4_ARM_SmallPageObject, seL4_PageBits, cur_cspace, &(ft[idx].cap));
    conditional_panic(err, "Unable to alloc frame(retype)");

    /* Map in page for SOS */
    err = map_page(ft[idx].cap, seL4_CapInitThreadPD, svaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    conditional_panic(err, "Unable to alloc frame(map)");

    /* Set new frames to all 0 */
    memset((void*)svaddr, 0, PAGE_SIZE);

    free_head = ft[idx].next_free;
    
    ft[idx].is_freeable = freeable;
    ft[idx].is_swappable = swappable;

    sync_release(ft_lock);

    return svaddr;
}

seL4_Word frame_alloc_sos(bool freeable) {
    sync_acquire(ft_lock);
    /* Try and grab a free frame */
    seL4_Word svaddr = frame_alloc(freeable, false);
    if (svaddr == 0) {
        /* If that fails, swap out another frame so we can use it */
        int err = swapin_sos(&svaddr);
        if (err) {
            sync_release(ft_lock);
            return 0;
        }
    }
    sync_release(ft_lock);
    return svaddr;
}

int frame_free(seL4_Word svaddr) {
    if (svaddr < low_addr) {
        return EFAULT;
    }

    uint32_t idx = svaddr_to_frame_idx(svaddr);

    sync_acquire(ft_lock);
    if (!idx || !ft[idx].is_freeable) {
        sync_release(ft_lock);
        return EFAULT; 
    }

    if (free_head == 0) {
        free_head = idx;
    } else {
        ft[free_tail].next_free = idx;
    }
    free_tail = idx;
    ft[idx].next_free = 0;

    /* Unmap for SOS */
    int err = seL4_ARM_Page_Unmap(ft[idx].cap);
    conditional_panic(err, "Unable to unmap page(free)");

    /* Remove the capability itself */
    err = cspace_delete_cap(cur_cspace, ft[idx].cap);
    conditional_panic(err, "unable to delete cap(free)");

    kut_free(ft[idx].paddr, seL4_PageBits);

    ft[idx].paddr = 0;
    ft[idx].user_cap = 0;
    ft[idx].cap = 0;
    ft[idx].vaddr_proc = NULL;
    ft[idx].vaddr = 0;
    ft[idx].referenced = false;

    sync_release(ft_lock);

    return 0;
}

uint32_t svaddr_to_frame_idx(seL4_Word svaddr) {
    if (svaddr < low_addr) return 0;

    return (svaddr - low_addr) / PAGE_SIZE;
}

seL4_Word frame_idx_to_svaddr(uint32_t idx) {
    return low_addr + PAGE_SIZE*idx;
}

int frame_get_swappable(seL4_Word svaddr) {
    uint32_t idx = svaddr_to_frame_idx(svaddr);

    if (!idx) {
        return EFAULT;
    }
    conditional_panic(idx >= num_frames, "frame_get_swappable invalid idx");
    
    sync_acquire(ft_lock);
    bool swappable = ft[idx].is_swappable;
    sync_release(ft_lock);

    return swappable;
}

int frame_change_swappable(seL4_Word svaddr, bool swappable) {
    uint32_t idx = svaddr_to_frame_idx(svaddr);

    if (!idx) {
        return EFAULT;
    }
    conditional_panic(idx >= num_frames, "frame_change_swappable invalid idx");
    
    sync_acquire(ft_lock);
    ft[idx].is_swappable = swappable;
    sync_release(ft_lock);

    return 0;
}

int frame_change_permissions(seL4_Word svaddr, seL4_CapRights rights, seL4_ARM_VMAttributes attr) {
    uint32_t idx = svaddr_to_frame_idx(svaddr);

    if (!idx) {
        return EFAULT;
    }
    sync_acquire(ft_lock);

    int err = seL4_ARM_Page_Unmap(ft[idx].cap);
    conditional_panic(err, "Unable to unmap page(change permissions)");

    
    err = map_page(ft[idx].cap, seL4_CapInitThreadPD, svaddr, rights, attr);
    sync_release(ft_lock);

    return err;
}

void get_ft_limits(size_t *ret_lo, size_t *ret_hi) {
    if (ret_lo != NULL) *ret_lo = first_valid_frame;
    if (ret_hi != NULL) *ret_hi = num_frames;
}
