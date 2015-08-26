#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <addrspace.h>

int pt_add_page(sos_process_t *proc, seL4_Word addr, seL4_Word *kaddr, seL4_CPtr *frame_cap);

void pt_remove_page(struct pt_entry *pe);

struct pt_entry *vaddr_to_pt_entry(struct addrspace *as, seL4_Word vaddr);

#endif /* _PAGETABLE_H_ */