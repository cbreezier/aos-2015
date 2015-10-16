/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#ifndef _CLOCK_H_
#define _CLOCK_H_

#include <sel4/sel4.h>

/*
 * Return codes for driver functions
 */
#define CLOCK_R_OK     0        /* success */
#define CLOCK_R_UINT (-1)       /* driver not initialised */
#define CLOCK_R_CNCL (-2)       /* operation cancelled (driver stopped) */
#define CLOCK_R_FAIL (-3)       /* operation failed for other reason */

#define REGISTER_TIMER_CALL 0
#define REMOVE_TIMER_CALL 1
#define TIMESTAMP_CALL 2
#define STOP_TIMER_CALL 3

#define TIMER_CALLBACK_LABEL 6

typedef uint64_t timestamp_t;

/* 
 * List of timers, sorted by their order of activation.
 * Delay is relative to the previous timer in the list, such
 * rescheduling a timer can be done in constant time
 */
struct timer_list_node {
    uint64_t delay;

    uint32_t id;

    void *callback;
    void *data;

    seL4_CPtr reply_cap;

    struct timer_list_node *next;
};

#endif /* _CLOCK_H_ */
