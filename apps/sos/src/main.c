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

#include <cspace/cspace.h>

#include <cpio/cpio.h>
#include <nfs/nfs.h>
#include <elf/elf.h>
#include <serial/serial.h>
#include <clock/clock.h>
#include <utils/number_allocator.h>
#include <sos.h>


#include "frametable.h"
#include "network.h"
#include "elf.h"
#include "pagetable.h"

#include "ut_manager/ut.h"
#include "vmem_layout.h"
#include <utils/mapping.h>

#include <autoconf.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

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

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];

const seL4_BootInfo* _boot_info;

sos_process_t tty_test_process;


/*
 * A dummy starting syscall
 */
#define SOS_SYSCALL0 0

/* Serial handle */
struct serial *serial;

seL4_CPtr _sos_ipc_ep_cap;
seL4_CPtr _sos_interrupt_ep_cap;

/**
 * NFS mount point
 */
extern fhandle_t mnt_point;


void handle_syscall(seL4_Word badge, int num_args) {
    seL4_Word syscall_number;
    seL4_CPtr reply_cap;


    syscall_number = seL4_GetMR(0);

    /* Save the caller */
    reply_cap = cspace_save_reply_cap(cur_cspace);
    assert(reply_cap != CSPACE_NULL);

    /* Process system call */
    seL4_MessageInfo_t reply;
    seL4_Word buffer[350];
    size_t i;

    switch (syscall_number) {
    case SOS_SYSCALL0:
        dprintf(0, "syscall: thread made syscall 0!\n");

        reply = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR(0, 0);
        seL4_Send(reply_cap, reply);

        break;
    case 1:
        printf("SOS: Ending first process\n");
        end_first_process();
        printf("SOS: First process ended\n");
        
        break;
    case 2:
        // printf("length: %d\n", num_args);
        for (i = 0; i <= num_args / 4; i++) 
            buffer[i] = seL4_GetMR(i + 1);
        *((char *) buffer + num_args) = '\0';
        printf("buffer is: %s\n", buffer);
        serial_send(serial, (char *) buffer, num_args);

        // Reply so that we can context switch back to caller
        reply = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR(0, 0);
        seL4_Send(reply_cap, reply);

        break;
    default:
        printf("Unknown syscall %d\n", syscall_number);
        /* we don't want to reply to an unknown syscall */

    }

    /* Free the saved reply cap */
    cspace_free_slot(cur_cspace, reply_cap);
}

uint32_t a, b, c;
bool removed = false;

void syscall_loop(seL4_CPtr ep) {

    while (1) {
        seL4_Word badge;
        seL4_Word label;
        seL4_MessageInfo_t message;

//        if (time_stamp() > 12000000 && !removed) {
//            remove_timer(a);
//            removed = true;
//        }

        // int *p1 = 0xb0016004, *p2 = 0xb0017004;
        message = seL4_Wait(ep, &badge);
        label = seL4_MessageInfo_get_label(message);
        if(badge & IRQ_EP_BADGE){
            /* Interrupt */
            if (badge & IRQ_BADGE_TIMER) {
                timer_interrupt();
            }
            if (badge & IRQ_BADGE_NETWORK) {
                network_irq();
            }

        }else if(label == seL4_VMFault){
            /* Page fault */
            //dprintf(0, "vm fault at 0x%08x, pc = 0x%08x, %s\n", seL4_GetMR(1),
                    //seL4_GetMR(0),
                    //seL4_GetMR(2) ? "Instruction Fault" : "Data fault");

            if (badge == TTY_EP_BADGE) {
                //dprintf(0, "tty vm fault\n");
                //printf("Adding page\n");
                pt_add_page(&tty_test_process, seL4_GetMR(1), NULL, NULL);
                //printf("Done adding page\n");

                seL4_CPtr reply_cap = cspace_save_reply_cap(cur_cspace);

                seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
                seL4_SetMR(0, 0);

                seL4_Send(reply_cap, reply);
                cspace_free_slot(cur_cspace, reply_cap);
            } else {
                assert(!"Unable to handle vm faults");
            }
        }else if(label == seL4_NoFault) {
            /* System call */
            handle_syscall(badge, seL4_MessageInfo_get_length(message) - 1);

        }else{
            printf("Rootserver got an unknown message\n");
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
            dprintf(1,"| %3d   | %16s | %p | %12d |\n", i, name, data, size);
        }else{
            break;
        }
    }
    dprintf(1,"--------------------------------------------------------\n");
}

void start_first_process(char* app_name, seL4_CPtr fault_ep) {
    int err;

    //seL4_Word stack_addr;
    //seL4_CPtr stack_cap;
    seL4_CPtr user_ep_cap;

    uint32_t pid = 101;

    /* These required for setting up the TCB */
    seL4_UserContext context;

    /* These required for loading program sections */
    char* elf_base;
    unsigned long elf_size;

    printf("as initing\n");
    /* Initialise address space */
    as_init(&tty_test_process.as);

    printf("allocating vroot addr\n");
    /* Create a VSpace */
    tty_test_process.vroot_addr = ut_alloc(seL4_PageDirBits);
    conditional_panic(!tty_test_process.vroot_addr, 
                      "No memory for new Page Directory");
    err = cspace_ut_retype_addr(tty_test_process.vroot_addr,
                                seL4_ARM_PageDirectoryObject,
                                seL4_PageDirBits,
                                cur_cspace,
                                &tty_test_process.vroot);
    conditional_panic(err, "Failed to allocate page directory cap for client");

    printf("cspace create\n");
    /* Create a simple 1 level CSpace */
    tty_test_process.croot = cspace_create(1);
    assert(tty_test_process.croot != NULL);

    printf("adding IPC region\n");
    as_add_region(tty_test_process.as, PROCESS_IPC_BUFFER, PAGE_SIZE, 1, 1, 1);

    /* Create an IPC buffer */
    err = pt_add_page(&tty_test_process, PROCESS_IPC_BUFFER, NULL, &tty_test_process.ipc_buffer_cap);
    conditional_panic(err, "Unable to add page table page for ipc buffer\n");

    // tty_test_process.ipc_buffer_addr = ut_alloc(seL4_PageBits);
    // conditional_panic(!tty_test_process.ipc_buffer_addr, "No memory for ipc buffer");
    // err =  cspace_ut_retype_addr(tty_test_process.ipc_buffer_addr,
    //                              seL4_ARM_SmallPageObject,
    //                              seL4_PageBits,
    //                              cur_cspace,
    //                              &tty_test_process.ipc_buffer_cap);
    // conditional_panic(err, "Unable to allocate page for IPC buffer");

    printf("minting ep\n");
    /* Copy the fault endpoint to the user app to enable IPC */
    user_ep_cap = cspace_mint_cap(tty_test_process.croot,
                                  cur_cspace,
                                  fault_ep,
                                  seL4_AllRights, 
                                  seL4_CapData_Badge_new(pid));
    /* should be the first slot in the space, hack I know */
    assert(user_ep_cap == 1);
    assert(user_ep_cap == USER_EP_CAP);

    printf("tcb stuff\n");
    /* Create a new TCB object */
    tty_test_process.tcb_addr = ut_alloc(seL4_TCBBits);
    conditional_panic(!tty_test_process.tcb_addr, "No memory for new TCB");
    err =  cspace_ut_retype_addr(tty_test_process.tcb_addr,
                                 seL4_TCBObject,
                                 seL4_TCBBits,
                                 cur_cspace,
                                 &tty_test_process.tcb_cap);
    conditional_panic(err, "Failed to create TCB");

    /* Configure the TCB */
    printf("more tcb stuff\n");
    err = seL4_TCB_Configure(tty_test_process.tcb_cap, user_ep_cap, TTY_PRIORITY,
                             tty_test_process.croot->root_cnode, seL4_NilData,
                             tty_test_process.vroot, seL4_NilData, PROCESS_IPC_BUFFER,
                             tty_test_process.ipc_buffer_cap);
    conditional_panic(err, "Unable to configure new TCB");


    /* parse the cpio image */
    dprintf(1, "\nStarting \"%s\"...\n", app_name);
    elf_base = cpio_get_file(_cpio_archive, app_name, &elf_size);
    conditional_panic(!elf_base, "Unable to locate cpio header");

    /* load the elf image */
    printf("elf load\n");
    err = elf_load(&tty_test_process, elf_base);
    conditional_panic(err, "Failed to load elf image");


    /* Create a stack frame */
    /* Define stack */
    printf("adding heap and stack regions\n");
    as_add_heap(tty_test_process.as);
    as_add_stack(tty_test_process.as);
//    stack_addr = ut_alloc(seL4_PageBits);
//    conditional_panic(!stack_addr, "No memory for stack");
//    err =  cspace_ut_retype_addr(stack_addr,
//                                 seL4_ARM_SmallPageObject,
//                                 seL4_PageBits,
//                                 cur_cspace,
//                                 &stack_cap);
//    conditional_panic(err, "Unable to allocate page for stack");

    /* Map in the stack frame for the user app */
   // err = map_page(stack_cap, tty_test_process.vroot,
   //                PROCESS_STACK_TOP - (1 << seL4_PageBits),
   //                seL4_AllRights, seL4_ARM_Default_VMAttributes);
   // conditional_panic(err, "Unable to map stack IPC buffer for user app");

    /* Map in the IPC buffer for the thread */
    // err = map_page(tty_test_process.ipc_buffer_cap, tty_test_process.vroot,
    //                PROCESS_IPC_BUFFER,
    //                seL4_AllRights, seL4_ARM_Default_VMAttributes);
    // conditional_panic(err, "Unable to map IPC buffer for user app");

    /* Start the new process */
    printf("almost done!\n");
    memset(&context, 0, sizeof(context));
    context.pc = elf_getEntryPoint(elf_base);
    context.sp = PROCESS_STACK_TOP;
    seL4_TCB_WriteRegisters(tty_test_process.tcb_cap, 1, 0, 2, &context);
}

static void end_first_process(void) {
    int err;
    
    /* Destroy tcb */
    err = cspace_revoke_cap(cur_cspace, tty_test_process.tcb_cap);
    conditional_panic(err, "unable to revoke tcb cap");

    err = cspace_delete_cap(cur_cspace, tty_test_process.tcb_cap);
    conditional_panic(err, "unable to delete tcb cap");

    ut_free(tty_test_process.tcb_addr, seL4_TCBBits);

    /* Destroy process ipc cap */
    // TODO not sure how to do this without having a reference to user_ep_cap

    /* Destroy process cspace */
    err = cspace_destroy(tty_test_process.croot);
    conditional_panic(err, "unable to destroy cspace");

    /* Destroy page directory */
    err = cspace_revoke_cap(cur_cspace, tty_test_process.vroot);
    conditional_panic(err, "unable to revoke vroot");

    err = cspace_delete_cap(cur_cspace, tty_test_process.vroot);
    conditional_panic(err, "unable to delete vroot");
    
    ut_free(tty_test_process.vroot_addr, seL4_PageDirBits);

    /* Destroy address space */
    err = as_destroy(tty_test_process.as);
    conditional_panic(err, "unable to destroy address space");
}

static void _sos_ipc_init(seL4_CPtr* ipc_ep, seL4_CPtr* async_ep){
    seL4_Word ep_addr, aep_addr;
    int err;

    /* Create an Async endpoint for interrupts */
    aep_addr = ut_alloc(seL4_EndpointBits);
    conditional_panic(!aep_addr, "No memory for async endpoint");
    err = cspace_ut_retype_addr(aep_addr,
                                seL4_AsyncEndpointObject,
                                seL4_EndpointBits,
                                cur_cspace,
                                async_ep);
    conditional_panic(err, "Failed to allocate c-slot for Interrupt endpoint");

    /* Bind the Async endpoint to our TCB */
    err = seL4_TCB_BindAEP(seL4_CapInitThreadTCB, *async_ep);
    conditional_panic(err, "Failed to bind ASync EP to TCB");


    /* Create an endpoint for user application IPC */
    ep_addr = ut_alloc(seL4_EndpointBits);
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
    err = cspace_root_task_bootstrap(ut_alloc, ut_free, ut_translate,
                                     malloc, free);
    conditional_panic(err, "Failed to initialise the c space\n");

    /* Initialise DMA memory */
    err = dma_init(dma_addr, DMA_SIZE_BITS);
    conditional_panic(err, "Failed to intiialise DMA memory\n");

    /* Initialiase other system compenents here */

    _sos_ipc_init(ipc_ep, async_ep);
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
    printf("Timer = %llu, Time: %llu, difference: %llu\n", *((uint64_t*) data), t, diff);
    a = register_timer(*((uint64_t*) data), setup_tick_timer, data);
}

static void test0() {
    printf("Starting test0\n");
    /* Allocate 10 pages and make sure you can touch them all */
    for (int i = 0; i < 10; i++) {
        /* Allocate a page */
        seL4_Word vaddr;
        frame_alloc(&vaddr);
        assert(vaddr);

        /* Test you can touch the page */
        *((int*)vaddr) = 0x37;
        assert(*((int*)vaddr) == 0x37);

        printf("Page #%d allocated at %p\n",  i, (void *) vaddr);
    }
    printf("test0 done\n");
}

static void test1() {
    printf("Starting test1\n");
    /* Test that you eventually run out of memory gracefully,
     *    and doesn't crash */
    for (int i = 0;; ++i) {
        /* Allocate a page */
        seL4_Word vaddr;
        frame_alloc(&vaddr);
        if (!vaddr) {
            printf("Page #%d allocated at %p\n",  i, (int*)vaddr);
            break;
        }


        /* Test you can touch the page */
        *((int*)vaddr) = 0x37;
        assert(*((int*)vaddr) == 0x37);
    }
    printf("test1 done\n");
}

static void test2() {
    printf("Starting test2\n");
    /* Test that you never run out of memory if you always free frames. 
     *     This loop should never finish */
    for (int i = 0;; i++) {
        /* Allocate a page */
        seL4_Word vaddr;
        uint32_t page = frame_alloc(&vaddr);
        assert(vaddr != 0);

        /* Test you can touch the page */
        *((int*)vaddr) = 0x37;
        assert(*((int*)vaddr) == 0x37);

        if (i % 10000 == 0) printf("Page #%d allocated at %p\n",  i, (int*)vaddr);

        frame_free(page);
    }
    printf("test2 done\n");
}

/*
 * Main entry point - called by crt.
 */
int main(void) {

    dprintf(0, "\nSOS Starting...\n");

    _sos_init(&_sos_ipc_ep_cap, &_sos_interrupt_ep_cap);

    frametable_init();

    /* Initialise the network hardware */
    network_init(badge_irq_ep(_sos_interrupt_ep_cap, IRQ_BADGE_NETWORK));

    /* Start the timer hardware */
    start_timer(badge_irq_ep(_sos_interrupt_ep_cap, IRQ_BADGE_TIMER));
//    uint64_t t1 = 1100000;
//    uint64_t t2 = 77004001;
//    uint64_t t3 = 400000;
//    setup_tick_timer(0, &t1);
//    setup_tick_timer(0, &t2);
//    setup_tick_timer(0, &t3);

    /* Start the user application */
    start_first_process(TTY_NAME, _sos_ipc_ep_cap);

    printf("tty test pid = %d\n", tty_test_process.pid);

    /* Initialise serial */
    serial = serial_init();

    //test0();
    //test1();
    //test2();

    /* Wait on synchronous endpoint for IPC */
    dprintf(0, "\nSOS entering syscall loop\n");
    syscall_loop(_sos_ipc_ep_cap);

    /* Not reached */
    return 0;
}


