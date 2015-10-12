#include <bits/errno.h>
#include <stdio.h>
#include <sys/panic.h>
#include <sys/debug.h>
#include <cspace/cspace.h>
#include <cpio/cpio.h>
#include <utils/mapping.h>
#include <string.h>
#include <clock/clock.h>
#include <elf/elf.h>
#include <sync/mutex.h>

#include "vmem_layout.h"
#include "ut_manager/ut.h"
#include "file.h"
#include "elf.h"
#include "pagetable.h"
#include "addrspace.h"
#include "proc.h"
#include "console.h"
#include "alloc_wrappers.h"
#include "frametable.h"


sync_mutex_t proc_table_lock;

void proc_init() {
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        processes[i].next_free = (i == MAX_PROCESSES - 1) ? -1 : i+1;
        processes[i].pid = -1;
        processes[i].proc_lock = sync_create_mutex();
        processes[i].next_pid = i;
    }
    procs_head_free = 0;
    procs_tail_free = MAX_PROCESSES - 1;

    proc_table_lock = sync_create_mutex();
    conditional_panic(!proc_table_lock, "Can't create proc table lock");
}

int proc_create(pid_t parent, char *program_name) {
    int err = 0;

    dprintf(0, "acquiring proc table lock\n");
    sync_acquire(proc_table_lock);
    if (procs_head_free == -1) {
        dprintf(0, "FAIL A");
        sync_release(proc_table_lock);
        return ENOMEM;
    }

    uint32_t idx = (uint32_t)procs_head_free;
    procs_head_free = processes[procs_head_free].next_free;
    sync_release(proc_table_lock);

    pid_t pid = processes[idx].next_pid;

    processes[idx].as = NULL;
    processes[idx].vroot_addr = 0;
    processes[idx].vroot = 0;
    processes[idx].croot = NULL;
    processes[idx].user_ep_cap = 0;
    processes[idx].tcb_addr = 0;
    processes[idx].tcb_cap = 0;
    processes[idx].proc_files = NULL;


    dprintf(0, "acquiring proc lock\n");
    sync_acquire(processes[idx].proc_lock);
    processes[idx].parent_proc = parent;
    processes[idx].pid = pid;
    processes[idx].next_pid = (pid + MAX_PROCESSES) & PID_MAX;
    processes[idx].size = 0;
    if (timer_ep) {
        processes[idx].stime = (unsigned)(time_stamp(timer_ep) / 1000);
    } else {
        processes[idx].stime = 0;
    }
    strncpy(processes[idx].command, program_name, NAME_MAX);
    processes[idx].command[NAME_MAX - 1] = '\0';
    processes[idx].wait_ep = 0;
    processes[idx].zombie = false;
    processes[idx].sos_thread_handling = false;

    /* These required for setting up the TCB */
    seL4_UserContext context;

    /* These required for loading program sections */
    //char* elf_base;
    //unsigned long elf_size;
    seL4_Word program_entrypoint = 0;

    dprintf(0, "as initing\n");
    /* Initialise address space */
    err = as_init(&processes[idx].as);
    if (err) {
        dprintf(0, "FAIL B");
        goto proc_create_end;
    }   

    dprintf(0, "allocating vroot addr\n");
    /* Create a VSpace */
    processes[idx].vroot_addr = kut_alloc(seL4_PageDirBits);
    if (!processes[idx].vroot_addr) {
        dprintf(0, "FAIL C");
        err = ENOMEM;
        goto proc_create_end;
    }

    err = cspace_ut_retype_addr(processes[idx].vroot_addr,
                                seL4_ARM_PageDirectoryObject,
                                seL4_PageDirBits,
                                cur_cspace,
                                &processes[idx].vroot);
    if (err == seL4_NotEnoughMemory) {
        dprintf(0, "FAIL D");
        err = ENOMEM;
        goto proc_create_end;
    }
    /* panic on any other errors */
    conditional_panic(err, "Failed to allocate page directory cap for client");

    dprintf(0, "cspace create\n");
    /* Create a simple 1 level CSpace */
    processes[idx].croot = cspace_create(1);
    if (!processes[idx].croot) {
        dprintf(0, "FAIL E");
        err = ENOMEM;
        goto proc_create_end;
    }

    dprintf(0, "adding IPC region\n");
    err = as_add_region(processes[idx].as, PROCESS_IPC_BUFFER, PAGE_SIZE, 1, 1, 1);
    if (err) {
        dprintf(0, "FAIL F");
        goto proc_create_end;
    }
    dprintf(0, "Added IPC region\n");

    /* Create an IPC buffer */
    seL4_Word ipcbuf_svaddr;
    err = pt_add_page(&processes[idx], PROCESS_IPC_BUFFER, &ipcbuf_svaddr, &processes[idx].ipc_buffer_cap);
    if (err) {
        dprintf(0, "FAIL G");
        goto proc_create_end;
    }
    frame_change_swappable(ipcbuf_svaddr, 0);

    dprintf(0, "minting ep\n");
    /* Copy the fault endpoint to the user app to enable IPC */
    processes[idx].user_ep_cap = cspace_mint_cap(processes[idx].croot,
                                  cur_cspace,
                                  _sos_ipc_ep_cap,
                                  seL4_AllRights, 
                                  seL4_CapData_Badge_new(pid));
    if (processes[idx].user_ep_cap != USER_EP_CAP) {
        dprintf(0, "FAIL H");
        err = ENOMEM;
        goto proc_create_end;   
    }

    dprintf(0, "tcb stuff\n");
    /* Create a new TCB object */
    processes[idx].tcb_addr = kut_alloc(seL4_TCBBits);
    if (!processes[idx].tcb_addr) {
        dprintf(0, "FAIL I");
        err = ENOMEM;
        goto proc_create_end;
    }
    err =  cspace_ut_retype_addr(processes[idx].tcb_addr,
                                 seL4_TCBObject,
                                 seL4_TCBBits,
                                 cur_cspace,
                                 &processes[idx].tcb_cap);
    if (err) {
        dprintf(0, "FAIL J");
        goto proc_create_end;
    }

    /* Configure the TCB */
    dprintf(0, "more tcb stuff\n");
    err = seL4_TCB_Configure(processes[idx].tcb_cap, processes[idx].user_ep_cap, USER_PRIORITY,
                             processes[idx].croot->root_cnode, seL4_NilData,
                             processes[idx].vroot, seL4_NilData, PROCESS_IPC_BUFFER,
                             processes[idx].ipc_buffer_cap);
    if (err) {
        dprintf(0, "FAIL K");
        goto proc_create_end;
    }


    ///* parse the cpio image */
    //dprintf(1, "\nStarting \"%s\"...\n", program_name);
    //elf_base = cpio_get_file(_cpio_archive, program_name, &elf_size);
    //if (!elf_base) {
    //    err = ENOENT;
    //    goto proc_create_end;
    //}

    /* load the elf image */
    dprintf(0, "elf load\n");
    err = elf_load(&processes[idx], program_name, &program_entrypoint);
    if (err) {
        dprintf(0, "error is %d\n", err);
        dprintf(0, "FAIL M");
        goto proc_create_end;
    }

    /* Create a stack frame */
    dprintf(0, "adding heap and stack regions\n");
    err = as_add_heap(processes[idx].as);
    if (err) {
        dprintf(0, "FAIL N");
        goto proc_create_end;
    }
    err = as_add_stack(&processes[idx]);
    if (err) {
        dprintf(0, "FAIL O");
        goto proc_create_end;
    }

    /* File descriptor table stuff */
    processes[idx].proc_files = kmalloc(sizeof(struct fd_entry) * OPEN_FILE_MAX);
    if (!processes[idx].proc_files) {
        dprintf(0, "FAIL P");
        err = ENOMEM;
        goto proc_create_end;
    }
    for (int i = 0; i < OPEN_FILE_MAX; ++i) {
        processes[idx].proc_files[i].used = false;
        processes[idx].proc_files[i].offset = 0;
        processes[idx].proc_files[i].open_file_idx = 0;
        processes[idx].proc_files[i].next_free = i == OPEN_FILE_MAX - 1 ? -1 : i + 1;
    }
    processes[idx].proc_files[0].next_free = 3;
    processes[idx].files_head_free = 0;
    processes[idx].files_tail_free = OPEN_FILE_MAX - 1;

    sync_acquire(open_files_lock);

    int open_entry = 0;
    bool exists = (strcmp(open_files[open_entry].file_obj.name, "console") == 0);

    if (exists) {
        open_files[open_entry].ref_count += 2;
    } else {
        open_files[open_entry].ref_count = 2;
        open_files[open_entry].file_obj.read = console_read;
        open_files[open_entry].file_obj.write = console_write;
        strcpy(open_files[open_entry].file_obj.name, "console");
    }
    sync_release(open_files_lock);

    for (int i = 1; i < 3; ++i) {
        processes[idx].proc_files[i].used = true;
        processes[idx].proc_files[i].open_file_idx = open_entry;
        //processes[idx].proc_files[i].mode = (i == 0) ? FM_READ : FM_WRITE;
        processes[idx].proc_files[i].mode = FM_WRITE;
    }

    /* Start the new process */
    dprintf(0, "almost done!\n");
    memset(&context, 0, sizeof(context));
    context.pc = program_entrypoint;
    context.sp = PROCESS_STACK_TOP;
    seL4_TCB_WriteRegisters(processes[idx].tcb_cap, 1, 0, 2, &context);
proc_create_end:
    sync_release(processes[idx].proc_lock);

    if (err) {
        proc_exit(&processes[idx]);
        return -err;
    }

    return pid;
}

void proc_exit(process_t *proc) {
    int err;

    dprintf(0, "acquiring proc lock\n");

    sync_acquire(proc->proc_lock);

    pid_t pid_parent = proc->parent_proc;
    pid_t pid_proc = proc->pid;
    uint32_t pid_idx = pid_proc & PROCESSES_MASK;

    proc->pid = -1;

    if (proc->proc_files) {
        /* Close all fd and free related data structures */
        sync_acquire(open_files_lock);
        for (int i = 0; i < OPEN_FILE_MAX; ++i) {
            struct fd_entry *fd_entry = &proc->proc_files[i];
            if (fd_entry->used) {
                open_files[fd_entry->open_file_idx].ref_count--;
            }
        }
        sync_release(open_files_lock);
        kfree(proc->proc_files);
    }

    if (proc->as) {
        /* Destroy address space */
        dprintf(0, "as destroying\n");
        err = as_destroy(proc);
        dprintf(0, "done\n");
        conditional_panic(err, "unable to destroy address space");
    }

    /* Destroy tcb */
    dprintf(0, "before tcb cap\n");
    if (proc->tcb_cap) {
        err = cspace_delete_cap(cur_cspace, proc->tcb_cap);
        conditional_panic(err, "unable to delete tcb cap");
    }
    dprintf(0, "before tcb addr\n");
    if (proc->tcb_addr) {
        kut_free(proc->tcb_addr, seL4_TCBBits);
    }

    dprintf(0, "before ep cap\n");
    if (proc->user_ep_cap) {
        /* Destroy process ipc cap */
        err = cspace_delete_cap(proc->croot, proc->user_ep_cap);
        conditional_panic(err, "unable to delete user ep cap");
    }

    dprintf(0, "before croot\n");
    if (proc->croot) {
        /* Destroy process cspace */
        err = cspace_destroy(proc->croot);
        conditional_panic(err, "unable to destroy cspace");
    }

    /* Destroy page directory */
    dprintf(0, "before vroot\n");
    if (proc->vroot) {
        err = cspace_delete_cap(cur_cspace, proc->vroot);
        conditional_panic(err, "unable to delete vroot");
    }
    dprintf(0, "before vroot addr\n");
    if (proc->vroot_addr) {
        kut_free(proc->vroot_addr, seL4_PageDirBits);
    }

    dprintf(0, "acquiring proc table lock\n");
    /* Place pid back into 'free' pids list */
    sync_acquire(proc_table_lock);
    if (procs_head_free == -1 || procs_tail_free == -1) {
        assert(procs_head_free == -1 && procs_tail_free == -1);
        procs_head_free = pid_idx;
        procs_tail_free = pid_idx;
        processes[pid_idx].next_free = -1;
    } else {
        processes[procs_tail_free].next_free = pid_idx;
        processes[pid_idx].next_free = -1;
        procs_tail_free = pid_idx;
    }
    sync_release(proc_table_lock);

    //char outbuf[100];
    //sprintf(outbuf, "Not all proc memory freed %d\n", proc->size);
    conditional_panic(proc->size != 0, "Not all memory freed");

    sync_release(proc->proc_lock);

    /* Signal possibly waiting parent */
    if (pid_parent != -1) {
        uint32_t parent_idx = pid_parent & PROCESSES_MASK;
        dprintf(0, "acquiring parent proc lock\n");
        sync_acquire(processes[parent_idx].proc_lock);
        if (processes[parent_idx].pid == pid_parent && processes[parent_idx].wait_ep) {
            if (processes[parent_idx].wait_pid == -1 || processes[parent_idx].wait_pid == pid_proc) {
                processes[parent_idx].wait_pid = pid_proc;
                seL4_Notify(processes[parent_idx].wait_ep, 0);
                processes[parent_idx].wait_ep = 0;
            }
        }

        sync_release(processes[parent_idx].proc_lock);
    }
    dprintf(0, "all done\n");
}
