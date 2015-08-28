#ifndef _COPY_H_
#define _COPY_H_

#include <sos.h>
#include <addrspace.h>


int copyin(sos_process_t *proc, void *dest, void *src, size_t nbytes); 

int copyinstring(sos_process_t *proc, void *dest, void *src, size_t nbytes);

int copyout(sos_process_t *proc, void *dest, void *src, size_t nbytes); 

int copyoutstring(sos_process_t *proc, void *dest, void *src, size_t nbytes); 

#endif /* _COPY_H_ */
