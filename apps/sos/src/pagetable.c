#include <utils/mapping.h>
#include <sel4/types.h>
#include <sel4/types_gen.h>
#include <utils/page.h>
#include <string.h>
#include <stdlib.h>
#include <sys/panic.h>
#include <sys/debug.h>
#include "swap.h"
#include "pagetable.h"
#include "frametable.h"
#include "alloc_wrappers.h"

int pt_add_page(process_t *proc, seL4_Word vaddr, seL4_Word *ret_svaddr, seL4_CPtr *ret_frame_cap) {

    int err = 0;

    vaddr = PAGE_ALIGN(vaddr, PAGE_SIZE);

    /* Ensure that the address is within a valid region */
    struct region_entry *cur = NULL;
    for (cur = proc->as->region_head; cur != NULL; cur = cur->next) {
        if (cur->start <= vaddr && cur->start + cur->size > vaddr) 
            break;
    }
    if (cur == NULL) {
        dprintf(0, "warning warning invalid region\n");
        return EACCES;
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

    /* Creates a pagetable for the user page, if one does not exist */
    if (proc->as->page_directory[tl_idx] == NULL) {
        assert(PAGE_SIZE == sizeof(struct pt_entry) * (1 << SECOND_LEVEL_SIZE));
        proc->as->page_directory[tl_idx] = (struct pt_entry *)frame_alloc_sos(true);
        if (proc->as->page_directory[tl_idx] == NULL) {
            dprintf(0, "warning warning can't allocate page table\n");
            return ENOMEM;
        }
        memset(proc->as->page_directory[tl_idx], 0, PAGE_SIZE);
    }

    sync_acquire(ft_lock);
    seL4_Word svaddr = 0;

    if (proc->as->page_directory[tl_idx][sl_idx].frame < 0) {
        /* 
         * Swapped out page. Simply swap it in.
         * Note that as there may be space in RAM due to freed frames,
         * we possibly provide swapin with an address to swap into.
         */
        svaddr = frame_alloc(1, 1);
        int err = swapin(proc, vaddr, &svaddr);
        if (err) {
            sync_release(ft_lock);
            dprintf(0, "warning warning swapped out page swapin");
            return err;
        }
    } else if (proc->as->page_directory[tl_idx][sl_idx].frame > 0) {
        /* Page is in memory, but it is unmapped */
        svaddr = proc->as->page_directory[tl_idx][sl_idx].frame;
    } else {
        /* Brand new page */
        proc->size++;
        svaddr = frame_alloc(1, 1);
        if (svaddr == 0) {
            int err = swapin(proc, vaddr, &svaddr);
            if (err) {
                sync_release(ft_lock);
                dprintf(0, "warning warning new page swapin");
                return err;
            }
            conditional_panic(!svaddr, "Swapin ret svaddr is 0");
        }
    }

    uint32_t frame_idx = svaddr_to_frame_idx(svaddr);
    conditional_panic(!frame_idx, "Invalid svaddr/frame_idx (pt_add_page)");

    if (ret_svaddr != NULL) {
        *ret_svaddr = svaddr;
        /* Pin frame if the caller requests svaddr */
        frame_change_swappable(svaddr, 0);
    }

    if (ret_frame_cap != NULL) {
        *ret_frame_cap = ft[frame_idx].cap;
    }
    
    seL4_ARM_PageTable pt_cap = 0;
    seL4_Word pt_addr = 0;

    /* Only map in if not already mapped in */
    if (!ft[frame_idx].user_cap) {
        seL4_CPtr user_cap = cspace_copy_cap(cur_cspace, cur_cspace, ft[frame_idx].cap, seL4_AllRights);
        err = usr_map_page(user_cap, proc->vroot, vaddr, cap_rights, cap_attr, &pt_cap, &pt_addr);
        if (err == seL4_NotEnoughMemory) {
            err = cspace_delete_cap(cur_cspace, user_cap);
            conditional_panic(err, "Could not delete cap (pt_add_page not enough memory)");
            frame_free(svaddr);
            sync_release(ft_lock);
            return ENOMEM;
        }
        /* Panic on all other errors */
        conditional_panic(err, "User page cannot be mapped in (pt_add_page");

        if (pt_cap && pt_addr) {
            proc->as->pt_caps[tl_idx] = pt_cap;
            proc->as->pt_addrs[tl_idx] = pt_addr;
        }

        ft[frame_idx].user_cap = user_cap;

        ft[frame_idx].referenced = true;
        ft[frame_idx].vaddr_proc = proc;
        ft[frame_idx].vaddr = vaddr;

        proc->as->page_directory[tl_idx][sl_idx].frame = svaddr;
    }

    sync_release(ft_lock);

    return 0;
}

void pt_remove_page(process_t *proc, struct pt_entry *pe) {
    conditional_panic(pe->frame == 0, "Trying to remove a non existing page");
    /* Can only change own size - no lock needed */
    proc->size--;
    conditional_panic(proc->size < 0, "Negative memory usage?");

    sync_acquire(ft_lock);
    if (pe->frame < 0) {
        free_swap_entry(-pe->frame);
        sync_release(ft_lock);
        return;
    }
    seL4_CPtr user_cap = ft[svaddr_to_frame_idx(pe->frame)].user_cap;
    if (user_cap) {
        seL4_ARM_Page_Unmap(user_cap);

        /* Remove the capability itself */
        int err = cspace_delete_cap(cur_cspace, user_cap);
        conditional_panic(err, "unable to delete cap(free)");
    }
    int err = frame_free(pe->frame);
    conditional_panic(err, "Unable to delete frame(pt_remove_page)");
    conditional_panic(ft[svaddr_to_frame_idx(pe->frame)].user_cap, "user cap not set to NULL");
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
