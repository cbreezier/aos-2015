#include <bits/errno.h>
#include <stdio.h>
#include <sys/panic.h>
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

sync_mutex_t proc_table_lock;

void proc_init() {
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        processes[i].next_free = (i == MAX_PROCESSES - 1) ? -1 : i+1;
        processes[i].pid = -1;
    }
    procs_head_free = 0;
    procs_tail_free = MAX_PROCESSES - 1;

    proc_table_lock = sync_create_mutex();
    conditional_panic(!proc_table_lock, "Can't create proc table lock");
}

int proc_create(pid_t parent, char *program_name) {
    int err = 0;

    sync_acquire(proc_table_lock);
    if (procs_head_free == -1) {
        return ENOMEM;
    }

    uint32_t pid = (uint32_t)procs_head_free;
    procs_head_free = processes[procs_head_free].next_free;
    sync_release(proc_table_lock);

    processes[pid].parent_proc = parent;
    processes[pid].pid = pid;
    processes[pid].size = 0;
    processes[pid].stime = (unsigned)(time_stamp() / 1000);
    strcpy(processes[pid].command, program_name);

    /* These required for setting up the TCB */
    seL4_UserContext context;

    /* These required for loading program sections */
    char* elf_base;
    unsigned long elf_size;

    printf("as initing\n");
    /* Initialise address space */
    as_init(&processes[pid].as);

    printf("allocating vroot addr\n");
    /* Create a VSpace */
    processes[pid].vroot_addr = ut_alloc(seL4_PageDirBits);
    conditional_panic(!processes[pid].vroot_addr, 
                      "No memory for new Page Directory");
    err = cspace_ut_retype_addr(processes[pid].vroot_addr,
                                seL4_ARM_PageDirectoryObject,
                                seL4_PageDirBits,
                                cur_cspace,
                                &processes[pid].vroot);
    conditional_panic(err, "Failed to allocate page directory cap for client");

    printf("cspace create\n");
    /* Create a simple 1 level CSpace */
    processes[pid].croot = cspace_create(1);
    assert(processes[pid].croot != NULL);

    printf("adding IPC region\n");
    as_add_region(processes[pid].as, PROCESS_IPC_BUFFER, PAGE_SIZE, 1, 1, 1);
    printf("Added IPC region\n");

    /* Create an IPC buffer */
    err = pt_add_page(&processes[pid], PROCESS_IPC_BUFFER, NULL, &processes[pid].ipc_buffer_cap);
    conditional_panic(err, "Unable to add page table page for ipc buffer\n");

    printf("minting ep\n");
    /* Copy the fault endpoint to the user app to enable IPC */
    processes[pid].user_ep_cap = cspace_mint_cap(processes[pid].croot,
                                  cur_cspace,
                                  _sos_ipc_ep_cap,
                                  seL4_AllRights, 
                                  seL4_CapData_Badge_new(pid));
    /* should be the first slot in the space, hack I know */
    assert(processes[pid].user_ep_cap == 1);
    assert(processes[pid].user_ep_cap == USER_EP_CAP);

    printf("tcb stuff\n");
    /* Create a new TCB object */
    processes[pid].tcb_addr = ut_alloc(seL4_TCBBits);
    conditional_panic(!processes[pid].tcb_addr, "No memory for new TCB");
    err =  cspace_ut_retype_addr(processes[pid].tcb_addr,
                                 seL4_TCBObject,
                                 seL4_TCBBits,
                                 cur_cspace,
                                 &processes[pid].tcb_cap);
    conditional_panic(err, "Failed to create TCB");

    /* Configure the TCB */
    printf("more tcb stuff\n");
    err = seL4_TCB_Configure(processes[pid].tcb_cap, processes[pid].user_ep_cap, USER_PRIORITY,
                             processes[pid].croot->root_cnode, seL4_NilData,
                             processes[pid].vroot, seL4_NilData, PROCESS_IPC_BUFFER,
                             processes[pid].ipc_buffer_cap);
    conditional_panic(err, "Unable to configure new TCB");


    /* parse the cpio image */
    dprintf(1, "\nStarting \"%s\"...\n", program_name);
    elf_base = cpio_get_file(_cpio_archive, program_name, &elf_size);
    conditional_panic(!elf_base, "Unable to locate cpio header");

    /* load the elf image */
    printf("elf load\n");
    err = elf_load(&processes[pid], elf_base);
    conditional_panic(err, "Failed to load elf image");


    /* Create a stack frame */
    /* Define stack */
    printf("adding heap and stack regions\n");
    as_add_heap(processes[pid].as);
    as_add_stack(&processes[pid]);

    /* File descriptor table stuff */
    processes[pid].proc_files = malloc(sizeof(struct fd_entry) * OPEN_FILE_MAX);
    for (int i = 0; i < OPEN_FILE_MAX; ++i) {
        processes[pid].proc_files[i].used = false;
        processes[pid].proc_files[i].offset = 0;
        processes[pid].proc_files[i].open_file_idx = 0;
        processes[pid].proc_files[i].next_free = i == OPEN_FILE_MAX - 1 ? -1 : i + 1;
    }
    processes[pid].files_head_free = 3;
    processes[pid].files_tail_free = OPEN_FILE_MAX - 1;

    sync_acquire(open_files_lock);

    int open_entry = 0;
    bool exists = (strcmp(open_files[open_entry].file_obj.name, "console") == 0);

    if (exists) {
        open_files[open_entry].ref_count += 3;
    } else {
        open_files[open_entry].ref_count = 3;
        open_files[open_entry].file_obj.read = console_read;
        open_files[open_entry].file_obj.write = console_write;
        strcpy(open_files[open_entry].file_obj.name, "console");
    }
    sync_release(open_files_lock);

    for (int i = 0; i < 3; ++i) {
        processes[pid].proc_files[i].used = true;
        processes[pid].proc_files[i].open_file_idx = open_entry;
        processes[pid].proc_files[i].mode = (i == 0) ? FM_READ : FM_WRITE;
    }

    /* Start the new process */
    printf("almost done!\n");
    memset(&context, 0, sizeof(context));
    context.pc = elf_getEntryPoint(elf_base);
    context.sp = PROCESS_STACK_TOP;
    seL4_TCB_WriteRegisters(processes[pid].tcb_cap, 1, 0, 2, &context);

    return pid;
}
