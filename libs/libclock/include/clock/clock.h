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

typedef uint64_t timestamp_t;
typedef void (*timer_callback_t)(uint32_t id, void *data);

/*
 * Register a callback to be called after a given delay
 *    delay:  Delay time in microseconds before callback is invoked
 *    callback: Function to be called
 *    data: Custom data to be passed to callback function
 *
 * Returns 0 on failure, otherwise an unique ID for this timeout
 */
uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data, struct timer_list_node *given_node, seL4_CPtr timer_ep);

/*
 * Remove a previously registered callback by its ID
 *  id: Unique ID returned by register_time
 * Returns CLOCK_R_OK iff successful. 
 */
int remove_timer(uint32_t id);

/*
 * Returns present time in microseconds since booting.
 *
 * Returns a negative value if failure.
 */
timestamp_t time_stamp(seL4_CPtr timer_ep);

/*
 * Stop clock driver operation.
 *
 * Returns CLOCK_R_OK iff successful.
 */
int stop_timer(seL4_CPtr timer_ep);

#endif /* _CLOCK_H_ */
