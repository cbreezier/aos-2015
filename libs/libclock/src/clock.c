#include <clock/clock.h>
#include <cspace/cspace.h>
#include <bits/limits.h>
#include <bits/errno.h>
#include <mapping.h>

#include <stdio.h>

#include <utils/number_allocator.h>

#define GPT_IRQ 87
#define EPIT1_IRQ 88
#define EPIT2_IRQ 89

#define EPIT_CLOCK_FREQUENCY_MHZ 66
/* Number of microseconds taken from a 66Mhz clock to go from 2^32-1 to 0. */
#define MAX_US_EPIT 65075262

/* Bit position of fields within the EPIT CR */
enum {
    EN = 0,
    ENMOD = 1,
    OCIEN = 2,
    RLD = 3,
    PRESCALER = 4,
    IOVW = 17,
    CLKSRC = 24
};

/* 
 * List of timers, sorted by their order of activation.
 * Delay is relative to the previous timer in the list, such
 * rescheduling a timer can be done in constant time
 */
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

/* Number allocator for timer id allocation */
struct number_allocator *allocator;

#define EPIT1_BASE_ADDRESS (void*) 0x20D0000
#define EPIT2_BASE_ADDRESS (void*) 0x20D4000

static volatile struct epit_clocks {
    unsigned int cr;
    unsigned int sr;
    unsigned int lr;
    unsigned int cmpr;
    unsigned int cnr;
} *epit_clocks[2];

/* Taken from network.c */
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
 
    epit_clocks[0] = map_device(EPIT1_BASE_ADDRESS, PAGE_SIZE);
    if (epit_clocks[0] == NULL) {
        return EFAULT;
    }
    epit_clocks[1] = map_device(EPIT2_BASE_ADDRESS, PAGE_SIZE);
    if (epit_clocks[1] == NULL) {
        return EFAULT;
    }
   
    /* Set up */ 
    epit_clocks[0]->cr &= ~(BIT(EN));
    epit_clocks[0]->cr |= BIT(RLD);    
    epit_clocks[0]->cr |= BIT(CLKSRC); // Set CLKSRC to peripheral clock
    epit_clocks[0]->cr &= ~(0xFFF << PRESCALER);
    epit_clocks[0]->cr |= BIT(OCIEN);
    epit_clocks[0]->cr |= BIT(ENMOD);
    epit_clocks[0]->cmpr = 0;

    epit_clocks[1]->cr &= ~(BIT(EN));
    epit_clocks[1]->cr &= ~(BIT(RLD));
    epit_clocks[1]->cr |= BIT(CLKSRC); // Set CLKSRC to peripheral clock
    epit_clocks[1]->cr &= ~(0xFFF << PRESCALER);
    epit_clocks[1]->cr |= BIT(OCIEN);
    epit_clocks[1]->cr |= BIT(ENMOD);

    epit_clocks[1]->cr |= BIT(IOVW);
    epit_clocks[1]->lr = 0xFFFFFFFF;
    epit_clocks[1]->cmpr = 0;
    epit_clocks[1]->cr &= ~(BIT(IOVW));


    /* enable */
    epit_clocks[0]->cr |= BIT(EN);
    epit_clocks[1]->cr |= BIT(EN);

    overflow_offset = 0;

    allocator = init_allocator();

    return 0;
}

static uint32_t us_to_clock_counter(uint64_t us) {
    if (us > MAX_US_EPIT) {
        return 0xFFFFFFFF;
    }
    return us*EPIT_CLOCK_FREQUENCY_MHZ;
}

static uint64_t clock_counter_to_us(uint64_t cnr) {
    return cnr/EPIT_CLOCK_FREQUENCY_MHZ;
}

/* 
 * Given a time (in us), sets up EPIT1 such that it
 * causes an interrupt in that much time. Note that if
 * the time is greater than MAX_US_EPIT, it'll simply cause
 * an interrupt in MAX_US_EPIT.
 */
static void reschedule(uint64_t delay) {
    epit_clocks[0]->cr |= BIT(IOVW);
    uint32_t clock_counter = us_to_clock_counter(delay);
    epit_clocks[0]->lr = clock_counter;
    epit_clocks[0]->cr &= ~(BIT(IOVW));
}

uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data) {
    bool rescheduling = false;

    struct list_node *node = malloc(sizeof(struct list_node));
    node->callback = callback;
    node->data = data;
    node->id = allocator_get_num(allocator);
    node->delay = delay;
    uint64_t current_us = clock_counter_to_us(epit_clocks[0]->cnr);
    if (head == NULL) {
        node->next = NULL;
        head = node; 
        rescheduling = true;
    } else {
        /* 
         * Need to consider that time has elapsed since the timer at
         * the front of the queue has started. This considers it, as
         * the value within the timer at the front of the queue is still
         * the original one.
         */
        uint64_t overcompensation;
        if (head->delay > MAX_US_EPIT) {
            overcompensation = MAX_US_EPIT - current_us;
        } else {
            overcompensation = head->delay - current_us;
        }
        uint64_t running_delay = 0;
        struct list_node *prev = NULL;
        struct list_node *cur = head;
        /* 
         * Find the position at which to insert the new timer.
         * "cur" will contain the element following the position
         * at which we need to insert it, and "prev" the position
         * prior to it.
         */
        while (cur != NULL && running_delay + (cur == head ? cur->delay - overcompensation : cur->delay) 
        <= delay) {
            running_delay += (cur == head ? cur->delay - overcompensation : cur->delay);
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

        /* Adjust the delay to be relative to the new timer (unless inserting at the head) */
        if (cur != NULL && prev != NULL) {
            cur->delay -= delay - running_delay;
        }
    }

    if (rescheduling) {
        reschedule(delay);
        /* Update the time of the immediate next timer (the one which used to be the first) */
        if (head->next != NULL) {
            /* Subtract the time that it has run since it was first */
            if (head->next->delay > MAX_US_EPIT) {
                head->next->delay -= MAX_US_EPIT - current_us;
            } else {
                head->next->delay = current_us;
            }
            head->next->delay -= delay;
        }
    }
    return node->id;
}

int remove_timer(uint32_t id) {
    if (head == NULL) return 0;
    if (head->id == id) {
        if (head->next == NULL) {
            allocator_release_num(allocator, head->id); 
            free(head);
            return 0;
        }
        /* Add the time that this first timer has already run onto the immediate next timer */
        uint32_t current_cnr = epit_clocks[0]->cnr;
        if (head->delay > MAX_US_EPIT) {
            head->next->delay += head->delay - MAX_US_EPIT - clock_counter_to_us(current_cnr);
        } else {
            head->next->delay += clock_counter_to_us(current_cnr);
        }

        struct list_node *to_free = head;
        head = head->next;
        allocator_release_num(allocator, to_free->id);
        free(to_free);

        reschedule(head->delay);
    } else {
        struct list_node *prev = NULL;
        struct list_node *cur = head;
        while (cur != NULL && cur->id != id) {
            prev = cur;
            cur = cur->next;
        }
        
        if (cur != NULL) {
            if (cur->next != NULL) {
                cur->next->delay += cur->delay;
            }
            prev->next = cur->next;
        }
    }
    return 0;
}

int timer_interrupt(void) {
    if (epit_clocks[0]->sr) {
        epit_clocks[0]->sr = 0xFFFFFFFF;
        if (head == NULL) return 0;
        if (head->delay > MAX_US_EPIT) {
            head->delay -= MAX_US_EPIT;
            if (head->delay <= MAX_US_EPIT) {
                reschedule(head->delay);
            }
        } else {
            struct list_node *to_free = head;
            head = head->next;
            /* 
             * Rescheduling must be done prior to callback, for
             * efficiency, and to handle the possibility that
             * the callback registers a new timer
             */
            if (head != NULL) {
                reschedule(head->delay);
            }
            to_free->callback(to_free->id, to_free->data);
            allocator_release_num(allocator, to_free->id);
            free(to_free);
        }
        int err = seL4_IRQHandler_Ack(clock_irqs[0].cap);
        assert(!err);
    } else if (epit_clocks[1]->sr) {
        epit_clocks[1]->sr = 0xFFFFFFFF;
        overflow_offset++;
        int err = seL4_IRQHandler_Ack(clock_irqs[1].cap);
        assert(!err);
    }
    return 0;
}

timestamp_t time_stamp(void) {
    return clock_counter_to_us(overflow_offset * (1ull << 32) + (1ull << 32) - epit_clocks[1]->cnr - 1);
}

int stop_timer(void) {
    epit_clocks[0]->cr &= ~(BIT(EN)); 
    epit_clocks[1]->cr &= ~(BIT(EN)); 

    epit_clocks[0]->sr = 0xFFFFFFFF;
    epit_clocks[1]->sr = 0xFFFFFFFF;

    free((struct epit_clocks*)epit_clocks[0]);
    free((struct epit_clocks*)epit_clocks[1]);

    destroy_allocator(allocator);

    struct list_node *prev;
    struct list_node *cur = head;
    while (cur != NULL) {
        prev = cur;
        cur = cur->next;
        free(prev);
    }
    return 0;
}
