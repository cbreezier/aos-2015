#include <clock/clock.h>
#include <cspace/cspace.h>
//#include "mapping.h"
#include "../../../apps/sos/src/mapping.h"

//#include <allocator/number_allocator.h>

#define GPT_IRQ 87
#define EPIT1_IRQ 88
#define EPIT2_IRQ 89

#define EPIT_CLOCK_FREQUENCY_MHZ 66
#define MAX_US_EPIT 32537631

/*
 * Relying on:
 *  - init_allocator()
 *  - uint32_t allocator_get_num()
 *  - allocator_release_num(uint32_t)
 *  - destroy_allocator()
 *
 */


enum {
    EN = 0,
    ENMOD = 1,
    OCIEN = 2,
    RLD = 3,
    PRESCALER = 4,
    IOVW = 17,
    CLKSRC = 24
};

struct list_node {
    uint64_t delay;

    uint32_t id;

    timer_callback_t callback;
    void *data;

    struct list_node *next;
} *head;


static struct clock_irq {
    int irq;
    seL4_IRQHandler cap;
} clock_irqs[2];

/* 
 * Number of times that the up-counter has overflowed. We use this to convert
 * a 32-bit timer into a 64-bit timestamp.
 */
uint32_t overflow_offset;

#define EPIT1_BASE_ADDRESS (void*) 0x20D0000
#define EPIT2_BASE_ADDRESS (void*) 0x20D4000

static struct epit_clocks {
    int *cr;
    int *sr;
    int *lr;
    int *cmpr;
    int *cnr;
} epit_clocks[2];

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

    head = NULL;

 //   int err = init_allocator();
 //   if (err) {
 //       return err;
 //   }

    epit_clocks[0].cr = map_device(EPIT1_BASE_ADDRESS, 4);
    epit_clocks[0].sr = map_device(EPIT1_BASE_ADDRESS+4, 4);
    epit_clocks[0].lr = map_device(EPIT1_BASE_ADDRESS+8, 4);
    epit_clocks[0].cmpr = map_device(EPIT1_BASE_ADDRESS+12, 4);
    epit_clocks[0].cnr = map_device(EPIT1_BASE_ADDRESS+16, 4);

    epit_clocks[1].cr = map_device(EPIT2_BASE_ADDRESS, 4);
    epit_clocks[1].sr = map_device(EPIT2_BASE_ADDRESS+4, 4);
    epit_clocks[1].lr = map_device(EPIT2_BASE_ADDRESS+8, 4);
    epit_clocks[1].cmpr = map_device(EPIT2_BASE_ADDRESS+12, 4);
    epit_clocks[1].cnr = map_device(EPIT2_BASE_ADDRESS+16, 4);

    /* Set up */ 
    *(epit_clocks[0].cr) &= ~(BIT(EN));
    *(epit_clocks[0].cr) |= BIT(RLD);    
    *(epit_clocks[0].cr) |= BIT(CLKSRC); // Set CLKSRC to peripheral clock
    *(epit_clocks[0].cr) &= ~(0xFFF << PRESCALER);
    *(epit_clocks[0].cr) |= BIT(OCIEN);

    *(epit_clocks[1].cr) &= ~(BIT(EN));
    *(epit_clocks[1].cr) &= ~(BIT(RLD));
    *(epit_clocks[1].cr) |= BIT(CLKSRC); // Set CLKSRC to peripheral clock
    *(epit_clocks[1].cr) &= ~(0xFFF << PRESCALER);
    *(epit_clocks[1].cr) |= BIT(OCIEN);

    *(epit_clocks[1].cmpr) = 0xFFFFFFFF;

    /* enable */
    *(epit_clocks[0].cr) |= BIT(EN);
    *(epit_clocks[1].cr) |= BIT(EN);

    return 0;
}

static uint32_t us_to_clock_counter(uint64_t us) {
    if (us > MAX_US_EPIT) {
        return 0xFFFFFFFF;
    }
    return us*EPIT_CLOCK_FREQUENCY_MHZ;
}

static uint64_t clock_counter_to_us(uint32_t cnr) {
    return cnr/EPIT_CLOCK_FREQUENCY_MHZ; 
}

static void reschedule(uint64_t delay) {
    *(epit_clocks[0].cr) |= BIT(IOVW);
    uint32_t clock_counter = us_to_clock_counter(delay);
    *(epit_clocks[0].lr) = clock_counter;
    *(epit_clocks[0].cmpr) = clock_counter;
    *(epit_clocks[0].cr) &= ~(BIT(IOVW));
}

uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data) {
    bool rescheduling = false;

    struct list_node *node = malloc(sizeof(struct list_node));
    node->callback = callback;
    node->data = data;
    //node->id = allocator_get_num(allocator);
    node->delay = delay;
    if (head == NULL) {
        node->next = NULL;
        head = node; 
        rescheduling = true;
    } else {
        uint64_t running_delay = 0;
        struct list_node *prev = NULL;
        struct list_node *cur = head;
        while (cur != NULL && running_delay + cur->delay <= delay) {
            running_delay += cur->delay;
            prev = cur;
            cur = cur->next;
        }

        node->next = cur;
        node->delay -= running_delay;
        if (prev == NULL) {
            head = node;
            rescheduling = true;
        } else {
            prev->next = node;
        }

        /* Adjust it to be relative to the current one (unless inserting at the head) */
        if (cur != NULL && prev != NULL) {
            cur->delay -= delay - running_delay;
        }
    }

    if (rescheduling) {
        reschedule(delay);
        /* Update the time of the immediate next timer (the one which used to be the first) */
        if (head->next != NULL) {
            /* Subtract the time that it has run since it was first */
            uint32_t current_cnr = *(epit_clocks[0].cnr);
            if (head->next->delay > (1ull << 32)) {
                head->next->delay -= (1ull << 32) - clock_counter_to_us(current_cnr);
            } else {
                head->next->delay = clock_counter_to_us(current_cnr);
            }
            head->next->delay -= delay;
        }
    }
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
