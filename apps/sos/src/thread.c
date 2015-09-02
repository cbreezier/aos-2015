#include "thread.h"
#include "frametable.h"
#include <sys/panic.h>
#include <cspace/cspace.h>
#include <string.h>
#include <stdio.h>
#include <ut_manager/ut.h>
#include <bits/limits.h>

void threads_init(void (*entry_point)(void), seL4_CPtr sos_interrupt_ep_cap) {
    for (size_t i = 1; i < NUM_SOS_THREADS; ++i) {
        struct sos_thread thread;

        /* Init IPC buffer */
        thread.ipc_addr = frame_alloc(1, 1);
        conditional_panic(!thread.ipc_addr, "Can't create IPC buffer - SOS thread");
        uint32_t frame_idx = vaddr_to_frame_idx(thread.ipc_addr);
        conditional_panic(!frame_idx, "Can't get IPC cap - SOS thread");
        thread.ipc_cap = ft[frame_idx].cap;

        printf("Ipc addr = %x\n", (uint32_t)thread.ipc_addr);


        /* Create TCB */
        thread.tcb_addr = ut_alloc(seL4_TCBBits);
        int err = cspace_ut_retype_addr(thread.tcb_addr,
                                        seL4_TCBObject,
                                        seL4_TCBBits,
                                        cur_cspace,
                                        &thread.tcb_cap); 
        conditional_panic(err, "Failed to create thread TCB - SOS thread");


        err = seL4_TCB_Configure(thread.tcb_cap,
                                 sos_interrupt_ep_cap, 
                                 255, /* Priority */
                                 cur_cspace->root_cnode,
                                 cur_cspace->guard,
                                 seL4_CapInitThreadVSpace,
                                 seL4_NilData,
                                 thread.ipc_addr,
                                 thread.ipc_cap);
        conditional_panic(err, "Failed to configure thread TCB - SOS thread");

        /* Allocate stack guard */
        thread.stack_top = frame_alloc(0, 0);
        conditional_panic(err, "Cannot allocate guard page 1");
        err = frame_change_permissions(thread.stack_top, 0, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);
        conditional_panic(err, "Cannot allocate guard page 2");
        /* Allocate stack memory */
        for (int i = 0; i < STACK_NUM_FRAMES; ++i) {
            seL4_Word next_frame = frame_alloc(0, 0);
            conditional_panic(!next_frame, "No memory for thread stack");
            assert(next_frame == thread.stack_top + PAGE_SIZE);
            thread.stack_top = next_frame;
        }
        thread.stack_top += PAGE_SIZE;
        

        seL4_UserContext context;
        memset(&context, 0, sizeof(context));
        context.pc = (seL4_Word)entry_point;
        context.sp = (seL4_Word)thread.stack_top;

        seL4_TCB_WriteRegisters(thread.tcb_cap, 1, 0, 2, &context);
        
        sos_threads[i] = thread; 
    }
}

