#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include "addrspace.h"

int pt_add_page(process_t *proc, seL4_Word vaddr, seL4_Word *ret_svaddr, seL4_CPtr *ret_frame_cap);

void pt_remove_page(process_t *proc, struct pt_entry *pe);

struct pt_entry *vaddr_to_pt_entry(struct addrspace *as, seL4_Word vaddr);

#endif /* _PAGETABLE_H_ */
