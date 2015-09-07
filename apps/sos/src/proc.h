#ifndef _PROC_H_
#define _PROC_H_

#include <sel4/sel4.h>
#include <cspace/cspace.h>
#include <limits.h>

#define N_NAME 32

struct addrspace;

typedef int pid_t;

struct fd_entry;

typedef struct {
    pid_t     pid;
    unsigned  size;       /* in pages */
    unsigned  stime;      /* start time in msec since booting */
    char      command[NAME_MAX];    /* Name of exectuable */

    seL4_Word tcb_addr;
    seL4_TCB tcb_cap;

    seL4_Word vroot_addr;
    seL4_ARM_PageDirectory vroot;

    seL4_Word ipc_buffer_addr;
    seL4_CPtr ipc_buffer_cap;

    seL4_CPtr user_ep_cap;

    cspace_t *croot;

    struct addrspace *as;

    struct fd_entry *proc_files;
    int files_head_free, files_tail_free;
} process_t;


#endif /* _PROC_H_ */
