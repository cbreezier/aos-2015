/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <limits.h>

#include <cspace/cspace.h>

#include <cpio/cpio.h>
#include <nfs/nfs.h>
#include <elf/elf.h>
#include <clock/clock.h>
#include <utils/number_allocator.h>
#include <sel4/constants.h>
//#include <sos.h>

#include "proc.h"
#include "thread.h"
#include "file.h"
#include "frametable.h"
#include "network.h"
#include "elf.h"
#include "pagetable.h"
#include "sos_syscall.h"
#include "console.h"
#include "swap.h"
#include "nfs_sync.h"
#include "alloc_wrappers.h"

#include "ut_manager/ut.h"
#include "vmem_layout.h"
#include <utils/mapping.h>

#include <autoconf.h>

#include <sys/debug.h>
#include <sys/panic.h>
#include <syscall.h>

/* This is the index where a clients syscall enpoint will
 * be stored in the clients cspace. */
#define USER_EP_CAP          (1)
/* To differencient between async and and sync IPC, we assign a
 * badge to the async endpoint. The badge that we receive will
 * be the bitwise 'OR' of the async endpoint badge and the badges
 * of all pending notifications. */
#define IRQ_EP_BADGE         (1 << (seL4_BadgeBits - 1))
/* All badged IRQs set high bet, then we use uniq bits to
 * distinguish interrupt sources */
#define IRQ_BADGE_NETWORK (1 << 0)
#define IRQ_BADGE_TIMER   (1 << 1)

#define TTY_NAME             CONFIG_SOS_STARTUP_APP
#define TTY_PRIORITY         (0)
#define TTY_EP_BADGE         (101)

const seL4_BootInfo* _boot_info;

process_t tty_test_process;

sync_mutex_t network_irq_lock;


/*
 * A dummy starting syscall
 */
#define SOS_SYSCALL0 0
#define SOS_MMAP2 192


#define NUM_SYSCALLS 378
typedef seL4_MessageInfo_t (*sos_syscall_t)(process_t *proc, int num_args);

sos_syscall_t syscall_jt[NUM_SYSCALLS];

/**
 * NFS mount point
 */
#define NFS_TICK_TIME 100000ull
extern fhandle_t mnt_point;

struct mutex_ep {
    seL4_CPtr unminted;
    uint32_t ep_addr;
};

void* sync_new_ep(seL4_CPtr* ep, int badge) {
    struct mutex_ep *ret = kmalloc(sizeof(struct mutex_ep));

    if (!ret) {
        return NULL;
    }   

    ret->ep_addr = kut_alloc(seL4_EndpointBits);
    if (ret->ep_addr == 0) {
        kfree(ret);
        return NULL;
    }

    int err = cspace_ut_retype_addr(ret->ep_addr, seL4_AsyncEndpointObject, seL4_EndpointBits, cur_cspace, &ret->unminted);
    if (err) {
        kfree(ret);
        return NULL;
    }

    *ep = cspace_mint_cap(cur_cspace, cur_cspace, ret->unminted, seL4_AllRights, seL4_CapData_Badge_new(badge));

    return (void*)ret;
}

void sync_free_ep(void *ep) {
    struct mutex_ep *to_free = (struct mutex_ep*)ep;

    int err = cspace_revoke_cap(cur_cspace, to_free->unminted);
    conditional_panic(err, "Unable to revoke cap(sync_free_eP)");

    err = cspace_delete_cap(cur_cspace, to_free->unminted);
    conditional_panic(err, "Unable to delete cap(sync_free_eP)");

    kut_free(to_free->ep_addr, seL4_EndpointBits);
}

seL4_MessageInfo_t unknown_syscall(process_t *proc, int num_args) {
    dprintf(0, "Unknown syscall %d\n", seL4_GetMR(0)); 

    seL4_MessageInfo_t reply;
    return reply;
}

void copyMR(seL4_Word *buf, bool in, size_t num_args) {
    for (int i = 0; i < num_args; ++i) {
        if (in) buf[i] = seL4_GetMR(i);
        else seL4_SetMR(i, buf[i]);
    }
}

void handle_syscall(seL4_Word badge, int num_args, seL4_CPtr reply_cap, seL4_Word *saved_mr) {
    seL4_Word syscall_number;

    syscall_number = saved_mr[0];

    /* Process system call */
    dprintf(-1, "got syscall number %u\n", syscall_number);

    seL4_MessageInfo_t reply;
    switch (syscall_number) {
    default:
        if (syscall_number >= NUM_SYSCALLS) {
            unknown_syscall(NULL, 0);   
        } else {

            process_t *proc = &processes[badge];

            sync_acquire(proc->proc_lock);
            if (proc->pid == -1) {
                sync_release(proc->proc_lock);
                goto handle_syscall_end;
            }
            proc->sos_thread_handling = true;
            sync_release(proc->proc_lock);

            copyMR(saved_mr, false, num_args + 1);

            reply = syscall_jt[syscall_number](proc, num_args);

            if (syscall_number != SYS_exit) {
                copyMR(saved_mr, true, seL4_MessageInfo_get_length(reply));

                sync_acquire(proc->proc_lock);
                proc->sos_thread_handling = false;
                sync_release(proc->proc_lock);

                copyMR(saved_mr, false, seL4_MessageInfo_get_length(reply));

                /* Check if zombie - if so kill the thread */
                if (proc->zombie) {
                    proc_exit(proc);    
                } else {
                    seL4_Send(reply_cap, reply);
                }
            }

            handle_syscall_end:
            cspace_free_slot(cur_cspace, reply_cap);
        }
        break;
    }

}

void syscall_loop(seL4_CPtr ep) {

    while (1) {
        seL4_Word badge;
        seL4_Word label;
        seL4_MessageInfo_t message;
        //dprintf(0, "waiting on syscall loop %x\n", get_cur_thread()->wakeup_async_ep);
        //dprintf(0, "W %x\n", get_cur_thread()->wakeup_async_ep);
        //dprintf(0, "W\n");
        message = seL4_Wait(ep, &badge);
        //dprintf(0, "G\n");
        //dprintf(0, "_");
        //dprintf(0, "got something in syscall loop %x\n", get_cur_thread()->wakeup_async_ep);
        label = seL4_MessageInfo_get_label(message);

        if(badge & IRQ_EP_BADGE){
            //dprintf(0, "badge %d\n", badge);

            /* Interrupt */
            if (badge & IRQ_BADGE_TIMER) {
                timer_interrupt();
            }
            if (badge & IRQ_BADGE_NETWORK) {
                sync_acquire(network_irq_lock);
                network_irq();
                sync_release(network_irq_lock);
            }

        }else if(label == seL4_VMFault){
            /* Save these as local variables, as message registers will change through lock acquiring */
            seL4_Word pc = seL4_GetMR(0);
            seL4_Word vaddr = seL4_GetMR(1);
            seL4_Word instruction_fault = seL4_GetMR(2);


            /* Page fault */
            dprintf(-1, "vm fault at 0x%08x, pc = 0x%08x, pid = %d, %s\n", vaddr,
                    pc, badge,
                    instruction_fault ? "Instruction Fault" : "Data fault");

            process_t *proc = &processes[badge];
            sync_acquire(proc->proc_lock);
            proc->sos_thread_handling = true;
            sync_release(proc->proc_lock);


            seL4_CPtr reply_cap = cspace_save_reply_cap(cur_cspace);
            assert(reply_cap != CSPACE_NULL);
            seL4_CPtr sos_cap;
            int err = pt_add_page(&processes[badge], vaddr, NULL, &sos_cap);

            switch(err) {
                case ENOMEM:
                case EACCES:
                    conditional_panic(err, "Segmentation Fault");
                default:
                    break;
            }

            conditional_panic(err, "failed to add page(vm fault)");

            if (1) {
                /* Flush cache entry */
                seL4_ARM_Page_Unify_Instruction(sos_cap, 0, PAGESIZE);
            }

            sync_acquire(proc->proc_lock);
            proc->sos_thread_handling = false;
            sync_release(proc->proc_lock);

            /* Check if zombie - if so kill the thread */
            if (proc->zombie) {
                proc_exit(proc);    
            } else {
                seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
                seL4_SetMR(0, 0);
                seL4_Send(reply_cap, reply);
            }
            cspace_free_slot(cur_cspace, reply_cap);
        }else if(label == seL4_NoFault) {
            /* System call */
            
            size_t num_args = seL4_MessageInfo_get_length(message) - 1;
            seL4_Word saved_mr[seL4_MsgMaxLength];
            copyMR(saved_mr, true, num_args + 1);

            seL4_CPtr reply_cap = cspace_save_reply_cap(cur_cspace);
            assert(reply_cap != CSPACE_NULL);

            handle_syscall(badge, num_args, reply_cap, saved_mr);

        } else if (label == seL4_UserException) {
            dprintf(0, "User exception, pid = %d\n", badge); 
        } else{
            dprintf(0, "Rootserver got an unknown message %d, pid = %d\n", label, badge);
        }
    }
}


static void print_bootinfo(const seL4_BootInfo* info) {
    int i;

    /* General info */
    dprintf(1, "Info Page:  %p\n", info);
    dprintf(1,"IPC Buffer: %p\n", info->ipcBuffer);
    dprintf(1,"Node ID: %d (of %d)\n",info->nodeID, info->numNodes);
    dprintf(1,"IOPT levels: %d\n",info->numIOPTLevels);
    dprintf(1,"Init cnode size bits: %d\n", info->initThreadCNodeSizeBits);

    /* Cap details */
    dprintf(1,"\nCap details:\n");
    dprintf(1,"Type              Start      End\n");
    dprintf(1,"Empty             0x%08x 0x%08x\n", info->empty.start, info->empty.end);
    dprintf(1,"Shared frames     0x%08x 0x%08x\n", info->sharedFrames.start, 
                                                   info->sharedFrames.end);
    dprintf(1,"User image frames 0x%08x 0x%08x\n", info->userImageFrames.start, 
                                                   info->userImageFrames.end);
    dprintf(1,"User image PTs    0x%08x 0x%08x\n", info->userImagePTs.start, 
                                                   info->userImagePTs.end);
    dprintf(1,"Untypeds          0x%08x 0x%08x\n", info->untyped.start, info->untyped.end);

    /* Untyped details */
    dprintf(1,"\nUntyped details:\n");
    dprintf(1,"Untyped Slot       Paddr      Bits\n");
    for (i = 0; i < info->untyped.end-info->untyped.start; i++) {
        dprintf(1,"%3d     0x%08x 0x%08x %d\n", i, info->untyped.start + i,
                                                   info->untypedPaddrList[i],
                                                   info->untypedSizeBitsList[i]);
    }

    /* Device untyped details */
    dprintf(1,"\nDevice untyped details:\n");
    dprintf(1,"Untyped Slot       Paddr      Bits\n");
    for (i = 0; i < info->deviceUntyped.end-info->deviceUntyped.start; i++) {
        dprintf(1,"%3d     0x%08x 0x%08x %d\n", i, info->deviceUntyped.start + i,
                                                   info->untypedPaddrList[i + (info->untyped.end - info->untyped.start)],
                                                   info->untypedSizeBitsList[i + (info->untyped.end-info->untyped.start)]);
    }

    dprintf(1,"-----------------------------------------\n\n");

    /* Print cpio data */
    dprintf(1,"Parsing cpio data:\n");
    dprintf(1,"--------------------------------------------------------\n");
    dprintf(1,"| index |        name      |  address   | size (bytes) |\n");
    dprintf(1,"|------------------------------------------------------|\n");
    for(i = 0;; i++) {
        unsigned long size;
        const char *name;
        void *data;

        data = cpio_get_entry(_cpio_archive, i, &name, &size);
        if(data != NULL){
            dprintf(1,"| %3d   | %16s | %p | %12lu |\n", i, name, data, size);
        }else{
            break;
        }
    }
    dprintf(1,"--------------------------------------------------------\n");
}

static void _sos_ipc_init(seL4_CPtr* ipc_ep, seL4_CPtr* async_ep){
    seL4_Word ep_addr, aep_addr;
    int err;

    /* Create an Async endpoint for interrupts */
    aep_addr = kut_alloc(seL4_EndpointBits);
    conditional_panic(!aep_addr, "No memory for async endpoint");
    err = cspace_ut_retype_addr(aep_addr,
                                seL4_AsyncEndpointObject,
                                seL4_EndpointBits,
                                cur_cspace,
                                async_ep);
    conditional_panic(err, "Failed to allocate c-slot for Interrupt endpoint");

    /* Bind the Async endpoint to our TCB */
    //err = seL4_TCB_BindAEP(seL4_CapInitThreadTCB, *async_ep);
    //conditional_panic(err, "Failed to bind ASync EP to TCB");


    /* Create an endpoint for user application IPC */
    ep_addr = kut_alloc(seL4_EndpointBits);
    conditional_panic(!ep_addr, "No memory for endpoint");
    err = cspace_ut_retype_addr(ep_addr, 
                                seL4_EndpointObject,
                                seL4_EndpointBits,
                                cur_cspace,
                                ipc_ep);
    conditional_panic(err, "Failed to allocate c-slot for IPC endpoint");
}


static void _sos_init(seL4_CPtr* ipc_ep, seL4_CPtr* async_ep){
    seL4_Word dma_addr;
    seL4_Word low, high;
    int err;

    /* Retrieve boot info from seL4 */
    _boot_info = seL4_GetBootInfo();
    conditional_panic(!_boot_info, "Failed to retrieve boot info\n");
    if(verbose > 0){
        print_bootinfo(_boot_info);
    }

    /* Initialise the untyped sub system and reserve memory for DMA */
    err = ut_table_init(_boot_info);
    conditional_panic(err, "Failed to initialise Untyped Table\n");
    /* DMA uses a large amount of memory that will never be freed */
    dma_addr = ut_steal_mem(DMA_SIZE_BITS);
    conditional_panic(dma_addr == 0, "Failed to reserve DMA memory\n");

    /* find available memory */
    ut_find_memory(&low, &high);

    /* Initialise the untyped memory allocator */
    ut_allocator_init(low, high);

    /* Initialise the cspace manager */
    err = cspace_root_task_bootstrap(kut_alloc, kut_free, ut_translate,
                                     kmalloc, kfree);
    conditional_panic(err, "Failed to initialise the c space\n");

    /* Initialise DMA memory */
    err = dma_init(dma_addr, DMA_SIZE_BITS);
    conditional_panic(err, "Failed to intiialise DMA memory\n");

    /* Initialiase other system compenents here */
    _sos_ipc_init(ipc_ep, async_ep);

    for (uint32_t i = 0; i < NUM_SYSCALLS; ++i) {
        syscall_jt[i] = unknown_syscall;
    }
    syscall_jt[0] = sos_null;
    syscall_jt[SYS_exit] = sos_exit;
    syscall_jt[SYS_mmap2] = sos_mmap2;
    syscall_jt[SYS_munmap] = sos_munmap;
    syscall_jt[SYS_nanosleep] = sos_nanosleep;
    syscall_jt[SYS_clock_gettime] = sos_clock_gettime;
    syscall_jt[SYS_brk] = sos_brk;
    syscall_jt[SYS_open] = sos_open;
    syscall_jt[SYS_close] = sos_close;
    syscall_jt[SYS_read] = sos_read;
    syscall_jt[SYS_write] = sos_write;
    syscall_jt[SYS_stat] = sos_stat;
    syscall_jt[SYS_getdents] = sos_getdents;
    syscall_jt[SYS_execve] = sos_execve;
    syscall_jt[SYS_getpid] = sos_getpid;
    syscall_jt[SYS_ustat] = sos_ustat;
    syscall_jt[SYS_waitid] = sos_waitid;
    syscall_jt[SYS_kill] = sos_kill;
}

static inline seL4_CPtr badge_irq_ep(seL4_CPtr ep, seL4_Word badge) {
    seL4_CPtr badged_cap = cspace_mint_cap(cur_cspace, cur_cspace, ep, seL4_AllRights, seL4_CapData_Badge_new(badge | IRQ_EP_BADGE));
    conditional_panic(!badged_cap, "Failed to allocate badged cap");
    return badged_cap;
}

timestamp_t last_time = 0;
void setup_tick_timer(uint32_t id, void *data) {
    timestamp_t t = time_stamp();
    timestamp_t diff = t - last_time;
    last_time = t;
    dprintf(0, "Timer = %llu, Time: %llu, difference: %llu\n", *((uint64_t*) data), t, diff);
    register_timer(*((uint64_t*) data), setup_tick_timer, data);
}

void nfs_tick(uint32_t id, void *data) {
    sync_acquire(nfs_lock);
    nfs_timeout();
    sync_release(nfs_lock);
    register_timer(NFS_TICK_TIME, nfs_tick, data);
}

//static void test0() {
//    dprintf(0, "Starting test0\n");
//    /* Allocate 10 pages and make sure you can touch them all */
//    for (int i = 0; i < 10; i++) {
//        /* Allocate a page */
//        seL4_Word vaddr = frame_alloc(1, 1);
//        assert(vaddr);
//
//        /* Test you can touch the page */
//        *((int*)vaddr) = 0x37;
//        assert(*((int*)vaddr) == 0x37);
//
//        dprintf(0, "Page #%d allocated at %p\n",  i, (void *) vaddr);
//    }
//    dprintf(0, "test0 done\n");
//}
//
//static void test1() {
//    dprintf(0, "Starting test1\n");
//    /* Test that you eventually run out of memory gracefully,
//     *    and doesn't crash */
//    for (int i = 0;; ++i) {
//        /* Allocate a page */
//        seL4_Word vaddr = frame_alloc(1, 1);
//        if (!vaddr) {
//            dprintf(0, "Page #%d allocated at %p\n",  i, (int*)vaddr);
//            break;
//        }
//
//
//        /* Test you can touch the page */
//        *((int*)vaddr) = 0x37;
//        assert(*((int*)vaddr) == 0x37);
//    }
//    dprintf(0, "test1 done\n");
//}
//
//static void test2() {
//    dprintf(0, "Starting test2\n");
//    /* Test that you never run out of memory if you always free frames. 
//     *     This loop should never finish */
//    for (int i = 0;; i++) {
//        /* Allocate a page */
//        seL4_Word vaddr = frame_alloc(1, 1);
//        assert(vaddr != 0);
//
//        /* Test you can touch the page */
//        *((int*)vaddr) = 0x37;
//        assert(*((int*)vaddr) == 0x37);
//
//        if (i % 10000 == 0) dprintf(0, "Page #%d allocated at %p\n",  i, (int*)vaddr);
//
//        frame_free(vaddr);
//    }
//    dprintf(0, "test2 done\n");
//}

void sos_sync_thread_entrypoint() {
    dprintf(0, "Ipc buffer = %x\n", (uint32_t)seL4_GetIPCBuffer());
    syscall_loop(_sos_ipc_ep_cap);

    assert(!"SOS thread has exited");
}

void sos_async_thread_entrypoint() {
    dprintf(0, "Ipc buffer = %x\n", (uint32_t)seL4_GetIPCBuffer());
    syscall_loop(_sos_interrupt_ep_cap);

    assert(!"SOS thread has exited");
}



/*
 * Main entry point - called by crt.
 */
int main(void) {

    dprintf(0, "\nSOS Starting...\n");

    _sos_init(&_sos_ipc_ep_cap, &_sos_interrupt_ep_cap);

    cur_cspace->lock = sync_create_mutex();
    conditional_panic(!cur_cspace->lock, "Cannot create cur_cspace lock");

    /* Initialise network irq lock */
    network_irq_lock = sync_create_mutex();
    conditional_panic(!network_irq_lock, "Cannot create network irq lock");

    /* Initialise alloc wrappers */
    alloc_wrappers_init();

    printf("frametable\n");
    frametable_init();

    /* Allocate all SOS threads */
    printf("thread init\n");
    threads_init(sos_async_thread_entrypoint, sos_sync_thread_entrypoint, _sos_interrupt_ep_cap);

    /* Initialise the network hardware */
    printf("network init\n");
    network_init(badge_irq_ep(_sos_interrupt_ep_cap, IRQ_BADGE_NETWORK));

    /* Init open file table */
    open_files_init();

    /* Initialise console */
    console_init();

    /* Initialise nfs sync */
    nfs_sync_init();

    /* Start the timer hardware */
    start_timer(badge_irq_ep(_sos_interrupt_ep_cap, IRQ_BADGE_TIMER));
    nfs_tick(0, NULL);

    /* Initialise swap table */
    size_t ft_lo_idx, ft_hi_idx;
    get_ft_limits(&ft_lo_idx, &ft_hi_idx);
    swap_init(ft_lo_idx, ft_hi_idx);

    proc_init();

    /* Start the user application */
    int pid = proc_create(-1, CONFIG_SOS_STARTUP_APP);
    //start_first_process(TTY_NAME, _sos_ipc_ep_cap);

    dprintf(0, "initial pid = %d\n", pid);

    /* Wait on synchronous endpoint for IPC */
    dprintf(0, "\nSOS entering syscall loop\n");
    dprintf(0, "Mains IPC = %x\n", (uint32_t)seL4_GetIPCBuffer());

    //syscall_loop(_sos_ipc_ep_cap);
    //seL4_Wait(sos_threads[0].wakeup_async_ep, NULL);

    printf("cur cpsace levels = %u\n", cur_cspace->levels);
    syscall_loop(_sos_interrupt_ep_cap);

    /* Not reached */
    return 0;
}


