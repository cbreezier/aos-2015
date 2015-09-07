#ifndef _THREAD_H_
#define _THREAD_H_

#include <sel4/sel4.h>

#define NUM_SOS_THREADS 2
#define STACK_NUM_FRAMES 1

struct sos_thread {
    uint32_t tcb_addr;
    seL4_TCB tcb_cap;

    seL4_Word ipc_addr;
    seL4_CPtr ipc_cap;

    seL4_Word stack_top;

    /* Used for notifying the thread when it is waiting on an async callback */
    seL4_CPtr wakeup_async_ep;
    uint32_t wakeup_ep_addr;
} sos_threads[NUM_SOS_THREADS];

void threads_init(void (*entry_point)(void), seL4_CPtr sos_interrupt_ep_cap);

struct sos_thread *get_cur_thread();

#endif /* _THREAD_H_ */
