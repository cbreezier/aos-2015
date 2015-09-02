#ifndef _SOS_SYSCALL_H_
#define _SOS_SYSCALL_H_
#include "proc.h"


void sos_mmap2(process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_munmap(process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_nanosleep(process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_clock_gettime(process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_brk(process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_open(process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_close(process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_read(process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_write(process_t *proc, seL4_CPtr reply_cap, int num_args); 

#endif /* _SOS_SYSCALL_H_ */
