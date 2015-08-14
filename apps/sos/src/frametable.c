#include <stdlib.h>
#include <frametable.h>
#include <ut_manager/ut.h>
#include <bits/limits.h>
#include <cspace/cspace.h>
#include <sys/panic.h>
#include <mapping.h>
#include <bits/errno.h>

#define ROUND_UP(n, b) (((((n) - 1ul) >> (b)) + 1ul) << (b))

/*
 * This frametable uses a linked list approach for finding the next
 * free frame. Each free frame has an index field referring to the next
 * frame in the list.
 *
 * Note that '0', while a valid frame, is always allocated to the
 * frametable itself. Therefore, it cannot be given away to a user, and
 * internally we use it in a similar way to NULL.
 */


struct ft_entry {
    seL4_Word seL4_id;
    seL4_CPtr cap;

    uint32_t next_free;
} *ft;

uint32_t free_head, free_tail;

seL4_Word low_addr, hi_addr, num_frames;


void frametable_init() {
    ut_find_memory(&low_addr, &hi_addr);

    seL4_Word memory_size = hi_addr - low_addr;

    num_frames = memory_size / PAGE_SIZE;
    seL4_Word frametable_size = num_frames * sizeof(struct ft_entry);
    frametable_size = ROUND_UP(frametable_size, seL4_PageBits);
    seL4_Word frametable_frames_required = frametable_size / PAGE_SIZE;

    ft = (struct ft_entry*) low_addr;
    free_head = frametable_frames_required;
    free_tail = num_frames-1;

    /* Allocate all frames required to hold the frametable data */
    for (uint32_t i = 0; i < frametable_frames_required; ++i) {
        seL4_Word id = ut_alloc(seL4_PageBits); 
        if (!id) {
            panic("Unable to init frametable(ut alloc)");
        }

        seL4_CPtr cap;
        int err = cspace_ut_retype_addr(id, seL4_ARM_SmallPageObject, seL4_PageBits, cur_cspace, &cap);
        conditional_panic(err, "Unable to init frametable(retype)");

        err = map_page(cap, seL4_CapInitThreadPD, low_addr + PAGE_SIZE*i, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        conditional_panic(err, "Unable to init frametable(map)");

        ft[i].seL4_id = id;
        ft[i].cap = cap;
        ft[i].next_free = 0;
    }

    /* Default initialise all frames which can be given away to users */
    for (uint32_t i = frametable_frames_required; i < num_frames; ++i) {
        ft[i].seL4_id = 0;
        ft[i].cap = 0;

        ft[i].next_free = (i == num_frames - 1) ? 0 : i+1;
    }
}

uint32_t frame_alloc(seL4_Word *vaddr) {
    if (free_head == 0) {
        *vaddr = 0;
        return 0;
    }

    int idx = free_head;

    ft[idx].seL4_id = ut_alloc(seL4_PageBits);
    if (!ft[idx].seL4_id) {
        *vaddr = 0;
        return 0;
    }

    *vaddr = low_addr + PAGE_SIZE*idx;

    int err = cspace_ut_retype_addr(ft[idx].seL4_id, seL4_ARM_SmallPageObject, seL4_PageBits, cur_cspace, &(ft[idx].cap));
    conditional_panic(err, "Unable to alloc frame(retype)");

    err = map_page(ft[idx].cap, seL4_CapInitThreadPD, *vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    conditional_panic(err, "Unable to alloc frame(map)");

    free_head = ft[idx].next_free;

    return idx;
}

int frame_free(uint32_t idx) {
    if (!idx) {
        return EFAULT; 
    }

    if (free_head == 0) {
        free_head = idx;
    } else {
        ft[free_tail].next_free = idx;
    }
    free_tail = idx;
    ft[idx].next_free = 0;

    seL4_ARM_Page_Unmap(ft[idx].cap);

    /* Remove all child capabilities */
    int err = cspace_revoke_cap(cur_cspace, ft[idx].cap);
    conditional_panic(err, "unable to revoke cap(free");

    /* Remove the capability itself */
    err = cspace_delete_cap(cur_cspace, ft[idx].cap);
    conditional_panic(err, "unable to delete cap(free");

    ut_free(ft[idx].seL4_id, seL4_PageBits);

    return 0;
}
