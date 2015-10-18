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

/* Exposed process information to the user */
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
    /* Linked list of free file descriptors */
    int files_head_free, files_tail_free;

    /* Next free pcb */
    int next_free;

    bool zombie;
    /* 
     * If an SOS thread is handling a syscall or vm fault, this
     * process must be set to a zombie, and cannot die until
     * the SOS thread finishes.
     */
    bool sos_thread_handling;

    /* Next pid associated with this pcb (increased by MAX_PROCESSES
     * on each process create). */
    pid_t next_pid;

    struct timer_list_node timer_sleep_node;
} process_t;

int procs_head_free, procs_tail_free;

process_t processes[MAX_PROCESSES];

void proc_init();

/*
 * Loads a program from the file system, and sets up all data necessary
 * for the process to run.
 */
int proc_create(pid_t parent, char *program_name);

/*
 * Destroys all data associated with the given process.
 */
void proc_exit(process_t *proc);

#endif /* _PROC_H_ */
