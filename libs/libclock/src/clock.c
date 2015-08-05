#include <clock/clock.h>
#include <cspace/cspace.h>

//#include "number_allocator.h"

#define GPT_IRQ 87
#define EPIT1_IRQ 88
#define EPIT2_IRQ 89

/*
 * Relying on:
 *  - init_allocator()
 *  - uint32_t allocator_get_num()
 *  - allocator_release_num(uint32_t)
 *  - destroy_allocator()
 *
 */

struct list_node {
    uint64_t time;

    void (*callback)(uint32_t id, void *data);
    void *data;
};


static struct clock_irq {
    int irq;
    seL4_IRQHandler cap;
} clock_irqs[2];

/* Taken from network.c - FIX */
static seL4_CPtr
enable_irq(int irq, seL4_CPtr aep, int *err) {
    seL4_CPtr cap;
    /* Create an IRQ handler */
    cap = cspace_irq_control_get_cap(cur_cspace, seL4_CapIRQControl, irq);
    if (!cap) {
        *err = 1;
        return cap;
    }
    /* Assign to an end point */
    *err = seL4_IRQHandler_SetEndpoint(cap, aep);
    if (*err) {
        return cap;
    }
    /* Ack the handler before continuing */
    *err = seL4_IRQHandler_Ack(cap);
    return cap;
}


int start_timer(seL4_CPtr interrupt_ep) {
    int err;
    clock_irqs[0].irq = EPIT1_IRQ;
    clock_irqs[0].cap = enable_irq(EPIT1_IRQ, interrupt_ep, &err);
    if (err) {
        return err;
    }
    
    clock_irqs[1].irq = EPIT2_IRQ;
    clock_irqs[1].cap = enable_irq(EPIT2_IRQ, interrupt_ep, &err);
    if (err) {
        return err;
    }

    //int err = init_allocator();
//    if (err) {
//        return err;
//    }
    return 0;
}

uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data) {
    return 0;
}

int remove_timer(uint32_t id) {
    return 0;
}

int timer_interrupt(void) {
    return 0;
}

timestamp_t time_stamp(void) {
    return 0;
}

int stop_timer(void) {
    return 0;
}
