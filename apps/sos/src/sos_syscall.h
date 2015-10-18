#ifndef _SOS_SYSCALL_H_
#define _SOS_SYSCALL_H_
#include "proc.h"

/* 
 * All of the below functions handle a syscall for a given process.
 *
 * Arguments are read from message registers, and return values also placed 
 * into message registers prior to returning.
 *
 * A seL4_MessageInfo_t object is returned, which is expected to be sent
 * back to the user process.
 *
 * They behave as per linux standards.
 */

seL4_MessageInfo_t sos_null(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_mmap2(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_munmap(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_nanosleep(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_clock_gettime(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_brk(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_open(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_close(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_read(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_write(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_stat(process_t *proc, int num_args); 

seL4_MessageInfo_t sos_getdents(process_t *proc, int num_args);

seL4_MessageInfo_t sos_execve(process_t *proc, int num_args);

seL4_MessageInfo_t sos_getpid(process_t *proc, int num_args);

seL4_MessageInfo_t sos_ustat(process_t *proc, int num_args);

seL4_MessageInfo_t sos_waitid(process_t *proc, int num_args);

seL4_MessageInfo_t sos_kill(process_t *proc, int num_args);

seL4_MessageInfo_t sos_exit(process_t *proc, int num_args);

#endif /* _SOS_SYSCALL_H_ */
