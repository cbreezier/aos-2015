#ifndef _SOS_SYSCALL_H_
#define _SOS_SYSCALL_H_
#include "proc.h"

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
