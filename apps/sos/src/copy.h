#ifndef _COPY_H_
#define _COPY_H_

#include "proc.h"
#include "addrspace.h"

bool user_buf_in_region(void *user_buf, size_t buf_size);

int user_buf_to_sos(sos_process_t *proc, void *usr_buf, size_t buf_size, seL4_Word *svaddr, size_t *buf_page_left);

int copyin(process_t *proc, void *dest, void *src, size_t nbytes); 

int copyinstring(process_t *proc, void *dest, void *src, size_t nbytes);

int copyout(process_t *proc, void *dest, void *src, size_t nbytes); 

int copyoutstring(process_t *proc, void *dest, void *src, size_t nbytes); 

#endif /* _COPY_H_ */
