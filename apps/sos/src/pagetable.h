#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <addrspace.h>

int pt_add_page(seL4_CPtr cap, sos_process_t *proc, seL4_Word addr);

#endif /* _PAGETABLE_H_ */
