/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sos.h>

#include <sel4/sel4.h>
#include <sel4/types.h>
#include <sel4/constants.h>
#include <syscall.h>

#define SYSCALL_ENDPOINT_SLOT (1)

int sos_sys_open(const char *path, fmode_t mode) {
    assert(!"You need to implement this");
    return -1;
}

int sos_sys_read(int file, char *buf, size_t nbyte) {
    assert(!"You need to implement this");
    return -1;
}

int sos_sys_write(int file, const char *buf, size_t nbyte) {
    assert(!"You need to implement this");
    return -1;
}

void sos_sys_usleep(int msec) {
    assert(!"You need to implement this");
}

int64_t sos_sys_time_stamp(void) {
    // assert(!"You need to implement this");
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 1*4);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_clock_gettime);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return seL4_GetMR(1);
}

