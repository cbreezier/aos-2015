#ifndef _COPY_H_
#define _COPY_H_

#include <sel4/sel4.h>
#include "proc.h"
#include "addrspace.h"


bool usr_buf_in_region(process_t *proc, void *usr_buf, size_t buf_size, bool *ret_region_r, bool *ret_region_w);

/* Make sure to make the frame swappable after using it */
int usr_buf_to_sos(process_t *proc, void *usr_buf, size_t buf_size, seL4_Word *ret_svaddr, size_t *ret_buf_page_left);

int copyin(process_t *proc, void *dest, void *src, size_t nbytes); 

int copyinstring(process_t *proc, void *dest, void *src, size_t nbytes);

int copyout(process_t *proc, void *dest, void *src, size_t nbytes); 

int copyoutstring(process_t *proc, void *dest, void *src, size_t nbytes); 

#endif /* _COPY_H_ */
