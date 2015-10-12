#ifndef _PROC_H_
#define _PROC_H_

#include <sel4/sel4.h>
#include <cspace/cspace.h>
#include <limits.h>
#include <sync/mutex.h>
#include <clock/clock.h>

#define MAX_PROCESSES 32
#define PROCESSES_MASK (MAX_PROCESSES - 1)

#define USER_EP_CAP          (1)
#define USER_PRIORITY (0)

#define PID_MAX ((int)((1u << (seL4_BadgeBits-1)) - 1))

seL4_CPtr _sos_ipc_ep_cap;
seL4_CPtr _sos_interrupt_ep_cap;

struct addrspace;

typedef int pid_t;

struct fd_entry;

typedef struct {
    pid_t     pid;
    unsigned  size;       /* in pages */
    unsigned  stime;      /* start time in msec since booting */
    char      command[NAME_MAX];    /* Name of exectuable */
} sos_process_t;


/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];

typedef struct {
    pid_t     pid;
    unsigned  size;       /* in pages */
    unsigned  stime;      /* start time in msec since booting */
    char      command[NAME_MAX];    /* Name of exectuable */

    seL4_Word tcb_addr;
    seL4_TCB tcb_cap;

    seL4_Word vroot_addr;
    seL4_ARM_PageDirectory vroot;

    seL4_CPtr ipc_buffer_cap;

    seL4_CPtr user_ep_cap;

    cspace_t *croot;

    pid_t parent_proc;

    sync_mutex_t proc_lock;

    /* Fields for wait_pid */
    seL4_CPtr wait_ep;
    pid_t wait_pid;

    struct addrspace *as;

    struct fd_entry *proc_files;
    int files_head_free, files_tail_free;

    int next_free;

    bool zombie;
    bool sos_thread_handling;

    pid_t next_pid;

    // struct timer_list_node timer_sleep_node;
} process_t;

int procs_head_free, procs_tail_free;

process_t processes[MAX_PROCESSES];

void proc_init();

int proc_create(pid_t parent, char *program_name);

void proc_exit(process_t *proc);

#endif /* _PROC_H_ */
