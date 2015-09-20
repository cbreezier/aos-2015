#include <sys/panic.h>
#include <cspace/cspace.h>
#include <string.h>
#include <stdio.h>
#include <bits/limits.h>
#include <bits/errno.h>
#include "thread.h"
#include "frametable.h"
#include "ut_manager/ut.h"
#include "alloc_wrappers.h"

seL4_Word low_ipc_addr = 0, high_ipc_addr = 0;
seL4_Word ipc_addr_diff = 0;

static int create_ep(seL4_CPtr *ep, uint32_t *ep_addr) {
    *ep_addr = kut_alloc(seL4_EndpointBits);
    if (*ep_addr == 0) {
        return ENOMEM;
    }

    int err = cspace_ut_retype_addr(*ep_addr, seL4_AsyncEndpointObject, seL4_EndpointBits, cur_cspace, ep);
    if (err) {
        return err;
    }
    return 0;
}

void threads_init(void (*async_entry_point)(void), void (*sync_entry_point)(void), seL4_CPtr sos_interrupt_ep_cap) {
    /* Create extra notification endpoint for main SOS thread */

    int err = create_ep(&sos_threads[0].wakeup_async_ep, &sos_threads[0].wakeup_ep_addr);
    conditional_panic(err, "Cannot create thread 0 ep");
    for (size_t i = 1; i < NUM_SOS_THREADS; ++i) {
        struct sos_thread thread;

        /* Init IPC buffer */
        thread.ipc_addr = frame_alloc_sos(false);
        conditional_panic(!thread.ipc_addr, "Can't create IPC buffer - SOS thread");
        uint32_t frame_idx = svaddr_to_frame_idx(thread.ipc_addr);
        conditional_panic(!frame_idx, "Can't get IPC cap - SOS thread");
        thread.ipc_cap = ft[frame_idx].cap;

        printf("Ipc addr = %x\n", (uint32_t)thread.ipc_addr);


        /* Create TCB */
        thread.tcb_addr = kut_alloc(seL4_TCBBits);
        err = cspace_ut_retype_addr(thread.tcb_addr,
                                        seL4_TCBObject,
                                        seL4_TCBBits,
                                        cur_cspace,
                                        &thread.tcb_cap); 
        conditional_panic(err, "Failed to create thread TCB - SOS thread");


        seL4_Uint8 priority = (i < NUM_ASYNC_SOS_THREADS) ? 255 : 254;
        err = seL4_TCB_Configure(thread.tcb_cap,
                                 sos_interrupt_ep_cap, 
                                 priority,
                                 cur_cspace->root_cnode,
                                 cur_cspace->guard,
                                 seL4_CapInitThreadVSpace,
                                 seL4_NilData,
                                 thread.ipc_addr,
                                 thread.ipc_cap);
        conditional_panic(err, "Failed to configure thread TCB - SOS thread");

        /* Allocate stack guard */
        thread.stack_top = frame_alloc_sos(false);
        conditional_panic(err, "Cannot allocate guard page 1");
        err = frame_change_permissions(thread.stack_top, 0, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);
        conditional_panic(err, "Cannot allocate guard page 2");
        /* Allocate stack memory */
        for (int i = 0; i < STACK_NUM_FRAMES; ++i) {
            seL4_Word next_frame = frame_alloc_sos(false);
            conditional_panic(!next_frame, "No memory for thread stack");
            assert(next_frame == thread.stack_top + PAGE_SIZE);
            thread.stack_top = next_frame;
        }
        thread.stack_top += PAGE_SIZE;
        

        seL4_UserContext context;
        memset(&context, 0, sizeof(context));
        if (i < NUM_ASYNC_SOS_THREADS) {
            context.pc = (seL4_Word)async_entry_point;
        } else {
            context.pc = (seL4_Word)sync_entry_point;
        }
        context.sp = (seL4_Word)thread.stack_top;

        seL4_TCB_WriteRegisters(thread.tcb_cap, 1, 0, 2, &context);
        
        sos_threads[i] = thread; 
        
        /* Get differences between ipc addrs */
        if (low_ipc_addr) {
            if (!ipc_addr_diff) {
                ipc_addr_diff = thread.ipc_addr - low_ipc_addr;
            } else {
                assert(thread.ipc_addr - high_ipc_addr == ipc_addr_diff);
            }
        } else {
            low_ipc_addr = thread.ipc_addr;
        }
        high_ipc_addr = thread.ipc_addr;
        high_ipc_addr = thread.ipc_addr;

        err = create_ep(&sos_threads[i].wakeup_async_ep, &sos_threads[i].wakeup_ep_addr);
        conditional_panic(err, "Cannot create thread 0 ep");

        /* Name thread (for debugging only) */
        char buf[20];
        sprintf(buf, "Thread %d", i);
        seL4_DebugNameThread(thread.tcb_cap, buf);
    }
}

struct sos_thread *get_cur_thread() {
    seL4_Word ipc_buf_addr = (seL4_Word)seL4_GetIPCBuffer();
    int idx = -1;
    if (ipc_buf_addr < low_ipc_addr || ipc_buf_addr > high_ipc_addr) {
        idx = 0;
    } else {
        if (NUM_SOS_THREADS == 2) {
            idx = 1;
        } else {
            idx = (ipc_buf_addr - low_ipc_addr) / ipc_addr_diff;
        }
    }
    return &sos_threads[idx];
}
