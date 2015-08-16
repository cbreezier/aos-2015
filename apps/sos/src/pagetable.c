#include <pagetable.h>
#include <mapping.h>
#include <sel4/types.h>
#include <frametable.h>

int pt_add_page(seL4_CPtr cap, sos_process_t *proc, seL4_Word vaddr) {

    vaddr = (vaddr / PAGE_SIZE) * PAGE_SIZE;

    struct region_entry *cur = NULL;
    for (cur = proc->as->region_head; cur != NULL; cur = cur->next) {
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
    }
    seL4_Word waste_of_memory;
    uint32_t frame_idx = frame_alloc(&waste_of_memory);
    
    seL4_ARM_PageTable pt_cap = 0;

    /* Only map at the end to prevent mapping before error checking */
    int err = sos_map_page(ft[frame_idx].cap, proc->vroot, vaddr, cap_rights, cap_attr, &pt_cap);
    if (err) {
        frame_free(frame_idx);
        return err;
    }

    if (pt_cap) {
        proc->as->page_caps[tl_idx] = pt_cap;
    }

    proc->as->page_directory[tl_idx][sl_idx].frame = frame_idx;
    return 0;
}
