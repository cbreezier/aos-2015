#include <utils/mapping.h>
#include <sel4/types.h>
#include <sel4/types_gen.h>
#include <string.h>
#include <stdlib.h>
#include <sys/panic.h>
#include "swap.h"
#include "pagetable.h"
#include "frametable.h"

int pt_add_page(process_t *proc, seL4_Word vaddr, seL4_Word *ret_svaddr, seL4_CPtr *frame_cap) {

    int err = 0;

    vaddr = (vaddr / PAGE_SIZE) * PAGE_SIZE;

    struct region_entry *cur = NULL;
    for (cur = proc->as->region_head; cur != NULL; cur = cur->next) {
        if (cur->start <= vaddr && cur->start + cur->size > vaddr) 
            break;
    }
    if (cur == NULL) {
        printf("warning warning d\n");
        /* TODO M7: segfault */
        return EINVAL;
    }

    seL4_CapRights cap_rights = 0;
    if (cur->r) cap_rights |= seL4_CanRead;
    if (cur->w) cap_rights |= seL4_CanWrite;
    if (cur->x) cap_rights |= seL4_CanRead;
    seL4_ARM_VMAttributes cap_attr = seL4_ARM_Default_VMAttributes;
    if (!cur->x) cap_attr |= seL4_ARM_ExecuteNever;

    /* Top level index */
    seL4_Word tl_idx = vaddr >> (SECOND_LEVEL_SIZE + OFFSET_SIZE);
    /* Second level index */
    seL4_Word sl_idx = (vaddr << TOP_LEVEL_SIZE) >> (TOP_LEVEL_SIZE + OFFSET_SIZE);

    if (proc->as->page_directory[tl_idx] == NULL) {
        assert(PAGE_SIZE == sizeof(struct pt_entry) * (1 << SECOND_LEVEL_SIZE));
        proc->as->page_directory[tl_idx] = (struct pt_entry *)frame_alloc_sos(true);
        if (proc->as->page_directory[tl_idx] == NULL) {
            printf("warning warning e\n");
            return ENOMEM;
        }
        memset(proc->as->page_directory[tl_idx], 0, PAGE_SIZE);
    }

    sync_acquire(ft_lock);
    seL4_Word svaddr = 0;
    /* Swapped out page */
    if (proc->as->page_directory[tl_idx][sl_idx].frame < 0) {
        svaddr = frame_alloc(1, 1);
        swapin(proc, vaddr, &svaddr);
        if (err) {
            printf("warning warning f\n");
            sync_release(ft_lock);
            return err;
        }
    } else if (proc->as->page_directory[tl_idx][sl_idx].frame > 0) {
        // simply map page back in and set reference to true
        svaddr = proc->as->page_directory[tl_idx][sl_idx].frame;
    } else {
        svaddr = frame_alloc(1, 1);
        if (svaddr == 0) {
            err = swapin(proc, vaddr, &svaddr);
            if (err) {
                printf("warning warning a\n");
                sync_release(ft_lock);
                return err;
            }
        }
    }

    uint32_t frame_idx = svaddr_to_frame_idx(svaddr);
    if (frame_idx == 0) {
        printf("warning warning b\n");
        sync_release(ft_lock);
        return ENOMEM;
    }
    if (ret_svaddr != NULL) {
        *ret_svaddr = svaddr;
    }


    if (frame_cap != NULL) {
        *frame_cap = ft[frame_idx].cap;
    }
    
    seL4_ARM_PageTable pt_cap = 0;
    seL4_Word pt_addr = 0;

    seL4_CPtr cap = cspace_copy_cap(cur_cspace, cur_cspace, ft[frame_idx].cap, seL4_AllRights);
    //printf("%x %x %x %u %u\n", cap, proc->vroot, vaddr, cap_rights, cap_attr);
    err = usr_map_page(cap, proc->vroot, vaddr, cap_rights, cap_attr, &pt_cap, &pt_addr);
    if (err) {
        printf("warning warning c err %d\n", err);
        frame_free(svaddr);
        sync_release(ft_lock);
        return err;
    }

    if (pt_cap && pt_addr) {
        proc->as->pt_caps[tl_idx] = pt_cap;
        proc->as->pt_addrs[tl_idx] = pt_addr;
    }

    ft[frame_idx].user_cap = cap;
    ft[frame_idx].referenced = true;
    ft[frame_idx].vaddr_proc = proc;
    ft[frame_idx].vaddr = vaddr;

    proc->as->page_directory[tl_idx][sl_idx].frame = svaddr;

    sync_release(ft_lock);

    return 0;
}

void pt_remove_page(struct pt_entry *pe) {
    sync_acquire(ft_lock);
    if (pe->frame < 0) {
        free_swap_entry(-pe->frame);
        sync_release(ft_lock);
        return;
    }
    seL4_CPtr user_cap = ft[svaddr_to_frame_idx(pe->frame)].user_cap;
    if (user_cap) {
        seL4_ARM_Page_Unmap(user_cap);

        /* Remove all child capabilities */
        int err = cspace_revoke_cap(cur_cspace, user_cap);
        conditional_panic(err, "unable to revoke cap(free)");

        /* Remove the capability itself */
        err = cspace_delete_cap(cur_cspace, user_cap);
        conditional_panic(err, "unable to delete cap(free)");
    }
    frame_free(pe->frame);
    sync_release(ft_lock);
}

struct pt_entry *vaddr_to_pt_entry(struct addrspace *as, seL4_Word vaddr) {
    /* Top level index */
    seL4_Word tl_idx = vaddr >> (SECOND_LEVEL_SIZE + OFFSET_SIZE);
    /* Second level index */
    seL4_Word sl_idx = (vaddr << TOP_LEVEL_SIZE) >> (TOP_LEVEL_SIZE + OFFSET_SIZE);
    if (as->page_directory[tl_idx] == NULL) {
        return NULL;
    }
    return &as->page_directory[tl_idx][sl_idx];
}
