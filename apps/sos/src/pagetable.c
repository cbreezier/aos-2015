#include <pagetable.h>
#include <utils/mapping.h>
#include <sel4/types.h>
#include <frametable.h>
#include <sel4/types_gen.h>
#include <string.h>
#include <stdlib.h>
#include <sys/panic.h>

int pt_add_page(sos_process_t *proc, seL4_Word vaddr, seL4_Word *kaddr, seL4_CPtr *frame_cap) {

    vaddr = (vaddr / PAGE_SIZE) * PAGE_SIZE;

    struct region_entry *cur = NULL;
    //printf("vaddr is %u\n", vaddr);
    for (cur = proc->as->region_head; cur != NULL; cur = cur->next) {
        //printf("considering region starting %u size %d\n", cur->start, cur->size);
        if (cur->start <= vaddr && cur->start + cur->size > vaddr) 
            break;
    }
    if (cur == NULL) {
        return EINVAL;
    }

    seL4_CapRights cap_rights = 0;
    if (cur->r) cap_rights |= seL4_CanRead;
    if (cur->w) cap_rights |= seL4_CanWrite;
    seL4_ARM_VMAttributes cap_attr = seL4_ARM_Default_VMAttributes;
    if (!cur->x) cap_attr |= seL4_ARM_ExecuteNever;

    /* Top level index */
    seL4_Word tl_idx = vaddr >> (SECOND_LEVEL_SIZE + OFFSET_SIZE);
    /* Second level index */
    seL4_Word sl_idx = (vaddr << TOP_LEVEL_SIZE) >> (TOP_LEVEL_SIZE + OFFSET_SIZE);

    if (proc->as->page_directory[tl_idx] == NULL) {
        proc->as->page_directory[tl_idx] = malloc(sizeof(struct pt_entry)*(1 << SECOND_LEVEL_SIZE));
        if (proc->as->page_directory[tl_idx] == NULL) {
            return ENOMEM;
        }
        memset(proc->as->page_directory[tl_idx], 0, sizeof(struct pt_entry)*(1 << SECOND_LEVEL_SIZE));
    }
    seL4_Word waste_of_memory;
    uint32_t frame_idx = frame_alloc(&waste_of_memory);
    if (frame_idx == 0) {
        return ENOMEM;
    }
    if (kaddr != NULL) {
        *kaddr = frame_idx_to_addr(frame_idx);
    }

    if (frame_cap != NULL) {
        *frame_cap = ft[frame_idx].cap;
    }
    
    seL4_ARM_PageTable pt_cap = 0;
    seL4_Word pt_addr = 0;

    ///* Only map at the end to prevent mapping before error checking */
    //printf("frame %u, cap %u, proc->vroot %u, vaddr %u\n", frame_idx, (uint32_t)ft[frame_idx].cap, (uint32_t)proc->vroot, (uint32_t)vaddr);

    //printf("minting %u %u\n", (uint32_t) cur_cspace, (uint32_t)proc->croot);
    seL4_CPtr cap = cspace_mint_cap(cur_cspace, cur_cspace, ft[frame_idx].cap, seL4_AllRights, seL4_CapData_Badge_new(proc->pid));

    int err = sos_map_page(cap, proc->vroot, vaddr, cap_rights, cap_attr, &pt_cap, &pt_addr);
    //printf("sos_map_page with error %u\n", err);
    if (err) {
        frame_free(frame_idx);
        return err;
    }

    if (pt_cap && pt_addr) {
        proc->as->pt_caps[tl_idx] = pt_cap;
        proc->as->pt_addrs[tl_idx] = pt_addr;
    }

    proc->as->page_directory[tl_idx][sl_idx].frame = frame_idx;
    proc->as->page_directory[tl_idx][sl_idx].cap = cap;
    return 0;
}

void pt_remove_page(struct pt_entry *pe) {
    seL4_ARM_Page_Unmap(pe->cap);

    /* Remove all child capabilities */
    int err = cspace_revoke_cap(cur_cspace, pe->cap);
    conditional_panic(err, "unable to revoke cap(free)");

    /* Remove the capability itself */
    err = cspace_delete_cap(cur_cspace, pe->cap);
    conditional_panic(err, "unable to delete cap(free)");

    frame_free(pe->frame);
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
