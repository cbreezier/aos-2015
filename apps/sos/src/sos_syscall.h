#ifndef _SOS_SYSCALL_H_
#define _SOS_SYSCALL_H_
#include <sos.h>


void sos_mmap2(sos_process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_munmap(sos_process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_nanosleep(sos_process_t *proc, seL4_CPtr reply_cap, int num_args); 

void sos_clock_gettime(sos_process_t *proc, seL4_CPtr reply_cap, int num_args); 

#endif /* _SOS_SYSCALL_H_ */
