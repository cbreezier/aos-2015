#ifndef _COPY_H_
#define _COPY_H_

#include "proc.h"
#include "addrspace.h"

int copyin(process_t *proc, void *dest, void *src, size_t nbytes); 

int copyinstring(process_t *proc, void *dest, void *src, size_t nbytes);

int copyout(process_t *proc, void *dest, void *src, size_t nbytes); 

int copyoutstring(process_t *proc, void *dest, void *src, size_t nbytes); 

#endif /* _COPY_H_ */
