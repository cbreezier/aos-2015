#ifndef _THREAD_H_
#define _THREAD_H_

#include <sel4/sel4.h>

#define NUM_SOS_THREADS 32
#define STACK_NUM_FRAMES 100

struct sos_thread {
    uint32_t tcb_addr;
    seL4_TCB tcb_cap;

    seL4_Word ipc_addr;
    seL4_CPtr ipc_cap;

    seL4_Word stack_top;
} sos_threads[NUM_SOS_THREADS];

void threads_init(void (*entry_point)(void), seL4_CPtr sos_interrupt_ep_cap);

#endif /* _THREAD_H_ */
