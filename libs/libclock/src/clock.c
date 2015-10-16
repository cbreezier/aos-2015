#include <clock/clock.h>
#include <cspace/cspace.h>
#include <bits/limits.h>
#include <bits/errno.h>
#include <utils/mapping.h>

#include <stdio.h>
#include <assert.h>

uint32_t register_timer(uint64_t delay, timer_callback_t callback, void *data, seL4_CPtr timer_ep) {
    printf("registering timer\n");
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 5);
    seL4_SetTag(tag);
    seL4_SetMR(0, REGISTER_TIMER_CALL);
    seL4_SetMR(1, (seL4_Word)delay);
    seL4_SetMR(2, (seL4_Word)(delay >> 32));
    seL4_SetMR(3, (seL4_Word)callback);
    seL4_SetMR(4, (seL4_Word)data);

    seL4_Call(timer_ep, tag);

    uint32_t id = seL4_GetMR(0);

    printf("sel4 call succeeded %u\n", id);

    return id;
}

int remove_timer(uint32_t id, seL4_CPtr timer_ep) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(0, REMOVE_TIMER_CALL);
    seL4_SetMR(1, (seL4_Word)id);
    seL4_Call(timer_ep, tag);

    return seL4_GetMR(0);
}

timestamp_t time_stamp(seL4_CPtr timer_ep) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 1);
    seL4_SetTag(tag);
    seL4_SetMR(0, TIMESTAMP_CALL);
    seL4_Call(timer_ep, tag);

    timestamp_t time = seL4_GetMR(1);
    time <<= 32;
    time |= seL4_GetMR(0);
    return time;
}

int stop_timer(seL4_CPtr timer_ep) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 1);
    seL4_SetTag(tag);
    seL4_SetMR(0, STOP_TIMER_CALL);
    seL4_Call(timer_ep, tag);

    return seL4_GetMR(0);
}
