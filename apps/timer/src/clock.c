#include <cspace/cspace.h>
#include <bits/limits.h>
#include <bits/errno.h>
#include <utils/mapping.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include <utils/number_allocator.h>

#include "clock.h"
#include <vmem_layout.h>

#define GPT_IRQ 87
#define EPIT1_IRQ 88
#define EPIT2_IRQ 89

// Caps copied in
#define SYSCALL_ENDPOINT_SLOT 1
#define TIMER_EP_CAP 2
#define TIMER_IRQ_CAP 3
#define CNODE_CAP 4

// Created here
#define EPIT1_CAP 5
#define EPIT2_CAP 6
#define REPLY_CAP 7

#define CSPACE_DEPTH 32

#define IRQ_BADGE_TIMER (1 << 1)

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

struct timer_list_node *head;


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
    unsigned int cr;   /* Control register */
    unsigned int sr;   /* Status register */
    unsigned int lr;   /* Load register */
    unsigned int cmpr; /* Compare register */
    unsigned int cnr;  /* Counter */
} *epit_clocks[2];

// Fixed assert
static void fassert(bool condition, char *err_msg) {
    if (condition) {
        printf("Assertion failed: %s\n", err_msg);
    }

    // Segfault and die
    *NULL;
}

/* Taken from network.c */
static seL4_CPtr
enable_irq(int irq, int slot) {
    /* Create an IRQ handler */
    int err = seL4_IRQControl_Get(seL4_CapIRQControl,
                               irq,
                               CNODE_CAP, 
                               slot, 
                               CSPACE_DEPTH);
    fassert(err, "Getting an IRQ control cap");

    /* Assign to an end point */
    err = seL4_IRQHandler_SetEndpoint(slot, TIMER_IRQ_CAP);
    fassert(err, "Assign irq to endpoint failed");

    /* Ack the handler before continuing */
    err = seL4_IRQHandler_Ack(slot);
    fassert(err, "Ack irq failed");

    return slot;
}

static int start_timer() {
    seL4_DebugPutChar('X');
    seL4_DebugPutChar('\n');
    clock_irqs[0].irq = EPIT1_IRQ;
    clock_irqs[0].cap = enable_irq(EPIT1_IRQ, EPIT1_CAP);
    
    clock_irqs[1].irq = EPIT2_IRQ;
    clock_irqs[1].cap = enable_irq(EPIT2_IRQ, EPIT2_CAP);

    /* Head of the list of registered timers */
    head = NULL;
    
    /* Map hardware address into a virtual address */
    epit_clocks[0] = (struct epit_clocks *)DEVICE_START;//map_device(EPIT1_BASE_ADDRESS, PAGE_SIZE);
    if (epit_clocks[0] == NULL) {
        return EFAULT;
    }
    epit_clocks[1] = (struct epit_clocks *)DEVICE_START;//map_device(EPIT2_BASE_ADDRESS, PAGE_SIZE);
    if (epit_clocks[1] == NULL) {
        return EFAULT;
    }
   
    /* Set up */ 
    epit_clocks[0]->cr &= ~(BIT(EN));  // Disable
    epit_clocks[0]->cr |= BIT(RLD);    // Set and forget mode
    epit_clocks[0]->cr |= BIT(CLKSRC); // Set CLKSRC to peripheral clock
    epit_clocks[0]->cr &= ~(0xFFF << PRESCALER);
    epit_clocks[0]->cr |= BIT(OCIEN);  // Generate interrupts upon compare event
    epit_clocks[0]->cr |= BIT(ENMOD);  // Load from LR upon overflow
    epit_clocks[0]->cmpr = 0;          // Generate compare event when CNR = 0

    epit_clocks[1]->cr &= ~(BIT(EN));  // Disable
    epit_clocks[1]->cr &= ~(BIT(RLD)); // Free running mode
    epit_clocks[1]->cr |= BIT(CLKSRC); // Set CLKSRC to peripheral clock
    epit_clocks[1]->cr &= ~(0xFFF << PRESCALER);
    epit_clocks[1]->cr |= BIT(OCIEN);  // Generate interrupts upon compare event
    epit_clocks[1]->cr |= BIT(ENMOD);  // Load 0xFFFFFFFF upon overflow
    epit_clocks[1]->cmpr = 0;          // Generate compare event when CNR = 0

    /* Set LR and CNR to maximum value to begin with */
    epit_clocks[1]->cr |= BIT(IOVW);
    epit_clocks[1]->lr = 0xFFFFFFFF;
    epit_clocks[1]->cr &= ~(BIT(IOVW));


    /* Enable EPIT 2 - Keep EPIT 1 disabled until a timer is registered */
    epit_clocks[1]->cr |= BIT(EN);

    overflow_offset = 0;

    //allocator = init_allocator(time_stamp());
    if (allocator == NULL) {
        return ENOMEM;
    }

    // printf("Finished initialising timer\n");

    return 0;
}

/* Microseconds to clock counter */
static uint32_t us_to_clock_counter(uint64_t us) {
    if (us > MAX_US_EPIT) {
        return 0xFFFFFFFF;
    }
    return us*EPIT_CLOCK_FREQUENCY_MHZ;
}

/* Clock counter to microseconds */
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
    epit_clocks[0]->cr |= BIT(EN);
}

static uint32_t register_timer(uint64_t delay, void *callback, void *data) {
    bool rescheduling = false;

    struct timer_list_node *node = malloc(sizeof(struct timer_list_node));
    assert(node != NULL);

    node->callback = callback;
    node->data = data;
    node->id = 0;//allocator_get_num(allocator);
    node->delay = delay;

    epit_clocks[0]->cr &= ~(BIT(EN));

    uint64_t current_us = 0;

    if (epit_clocks[0]->sr) {
        if (head != NULL) {
            current_us = head->delay < MAX_US_EPIT ? head->delay : MAX_US_EPIT;
        }
    } else {
        current_us = clock_counter_to_us(epit_clocks[0]->cnr);
    }

    epit_clocks[0]->cr |= BIT(EN);

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
        struct timer_list_node *prev = NULL;
        struct timer_list_node *cur = head;
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

        /*
         * When inserting a new timer, the relative delay of
         * the next timer is reduced by the relative delay of
         * the new timer.
         */
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
    int node_id = node->id;
    
    return node_id;
}

static int remove_timer(uint32_t id) {
    if (head == NULL) {
        return 0;
    }

    if (head->id == id) {
    
        epit_clocks[0]->cr &= ~(BIT(EN));

        /* Add the time that this first timer has already run onto the immediate next timer */
        uint32_t current_us;
        if (epit_clocks[0]->sr) {
            if (head != NULL) {
                current_us = head->delay < MAX_US_EPIT ? head->delay : MAX_US_EPIT;
            }
        } else {
            current_us = clock_counter_to_us(epit_clocks[0]->cnr);
        }

        epit_clocks[0]->cr |= BIT(EN);

        if (head->next == NULL) {
            //allocator_release_num(allocator, head->id); 
            if (!head->is_user_provided) {
                free(head);
            }
            return 0;
        }
        if (head->delay > MAX_US_EPIT) {
            head->next->delay += head->delay - MAX_US_EPIT - current_us;
        } else {
            head->next->delay += current_us;
        }

        /* Remove the timer and free its memory */
        struct timer_list_node *to_free = head;
        head = head->next;
        //allocator_release_num(allocator, to_free->id);
        if (!to_free->is_user_provided) {
            free(to_free);
        }

        /* Must reschedule timer since we modified the head */
        reschedule(head->delay);
    } else {
        /* List traversal to find the timer and remove it */
        struct timer_list_node *prev = NULL;
        struct timer_list_node *cur = head;
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

static void callback_reply(struct timer_list_node *node) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 3);
    seL4_SetTag(tag);
    seL4_SetMR(0, node->id);
    seL4_SetMR(1, (seL4_Word)node->data);
    seL4_SetMR(2, (seL4_Word)node->callback);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
}

static int timer_interrupt(void) {
    /*
     * Check which timer actually generated the interrupt.
     * The status register of that timer will be set.
     */
    if (epit_clocks[0]->sr) {
        epit_clocks[0]->sr = 0xFFFFFFFF;

        int err = seL4_IRQHandler_Ack(clock_irqs[0].cap);
        assert(!err);
        if (head == NULL) {
            assert(!"Timer interrupt with no queued timers");
            //int err = seL4_IRQHandler_Ack(clock_irqs[0].cap);
            //assert(!err);
            return 0;
        }

        /*
         * Timer isn't finished - just one loop of the EPIT
         * needs to be decremented.
         */
        if (head->delay > MAX_US_EPIT) {
            head->delay -= MAX_US_EPIT;
            if (head->delay < MAX_US_EPIT) {
                reschedule(head->delay);
            }
        } else {
            struct timer_list_node *to_free = head;
            head = head->next;
            to_free->next = NULL;
            /* 
             * Rescheduling must be done prior to callback, for
             * efficiency, and to handle the possibility that
             * the callback registers a new timer
             */
            if (head != NULL) {
                reschedule(head->delay);
            } else {
                epit_clocks[0]->cr &= ~(BIT(EN));
            }
            //allocator_release_num(allocator, to_free->id);

            callback_reply(to_free);

            if (!to_free->is_user_provided) {
                free(to_free);
            }
        }
    }
    if (epit_clocks[1]->sr) {
        epit_clocks[1]->sr = 0xFFFFFFFF;
        overflow_offset++;
        int err = seL4_IRQHandler_Ack(clock_irqs[1].cap);
        assert(!err);
    }
    return 0;
}

static timestamp_t time_stamp(void) {
    uint32_t epit1_cnr = epit_clocks[1]->cnr;
    if (epit_clocks[1]->sr) {
        timer_interrupt();
        epit1_cnr = epit_clocks[1]->cnr;
    } 
    /* 
     * Use value obtained before checking the status register,
     * incase it has ticked inbetween checking and returning
     */
    timestamp_t ret = clock_counter_to_us(overflow_offset * (1ull << 32) + (1ull << 32) - epit1_cnr - 1);
    return ret;
}

static int stop_timer(void) {
    epit_clocks[0]->cr &= ~(BIT(EN));
    epit_clocks[1]->cr &= ~(BIT(EN));

    epit_clocks[0]->sr = 0xFFFFFFFF;
    epit_clocks[1]->sr = 0xFFFFFFFF;

    free((struct epit_clocks*)epit_clocks[0]);
    free((struct epit_clocks*)epit_clocks[1]);

    //destroy_allocator(allocator);

    struct timer_list_node *prev;
    struct timer_list_node *cur = head;
    while (cur != NULL) {
        prev = cur;
        cur = cur->next;
        free(prev);
    }
    return 0;
}

static void timer_loop(seL4_CPtr ep) {
    while (true) {
        seL4_Word badge;
        seL4_Word label;
        seL4_MessageInfo_t message;

        message = seL4_Wait(ep, &badge);

        // Async timer interrupt
        if (badge & IRQ_BADGE_TIMER) {
            timer_interrupt();
            continue;
        }

        // Save reply cap
        seL4_Error err = seL4_CNode_SaveCaller(CNODE_CAP, REPLY_CAP, CSPACE_DEPTH);
        assert(!err);
        
        seL4_Word call_number = seL4_GetMR(0);
        seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
        switch (call_number) {
            case REGISTER_TIMER_CALL:
                asm("nop");
                uint64_t delay = ((uint64_t)seL4_GetMR(2) << 32) & (uint64_t)seL4_GetMR(1);
                uint32_t register_ret = register_timer(delay, (void*)seL4_GetMR(3), (void*)seL4_GetMR(4));

                seL4_SetMR(0, register_ret);
                seL4_Send(REPLY_CAP, reply);

                break;
            case REMOVE_TIMER_CALL:
                asm("nop");
                int remove_ret = remove_timer(seL4_GetMR(1));

                seL4_SetMR(0, remove_ret);
                seL4_Send(REPLY_CAP, reply);

                break;
            case TIMESTAMP_CALL:
                asm("nop");
                uint64_t timestamp_ret = time_stamp();

                seL4_SetMR(0, (seL4_Word)timestamp_ret);
                seL4_SetMR(1, (seL4_Word)(timestamp_ret >> 32));
                seL4_Send(REPLY_CAP, reply);

                break;
            case STOP_TIMER_CALL:
                asm("nop");
                int stop_ret = stop_timer();

                seL4_SetMR(0, stop_ret);
                seL4_Send(REPLY_CAP, reply);

                break;
            default:
                printf("Unknown timer call\n");
                break;
        }
    }
}

int main() {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 0);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    seL4_CPtr timer_ep_cap = seL4_GetCap(0);
    seL4_CPtr timer_irq_cap = seL4_GetCap(1);
    seL4_CPtr cnode_cap  = seL4_GetCap(2);
    printf("%u %u %u\n", timer_ep_cap, timer_irq_cap, cnode_cap);

    start_timer();

    timer_loop(TIMER_EP_CAP);
    
    stop_timer();
}
