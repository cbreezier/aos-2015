#ifndef _SOS_SYSCALL_H_
#define _SOS_SYSCALL_H_
#include <sos.h>


void sos_mmap2(sos_process_t *proc, seL4_MessageInfo_t *reply);

void sos_munmap(sos_process_t *proc, seL4_MessageInfo_t *reply);

void sos_time_stamp(sos_process_t *proc, seL4_MessageInfo_t *reply);

#endif /* _SOS_SYSCALL_H_ */
