/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 code.
 *      		     Libc will need sos_write & sos_read implemented.
 *
 *      Author:      Ben Leslie
 *
 ****************************************************************************/

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "ttyout.h"

#include <sel4/sel4.h>
#include <sel4/constants.h>

void ttyout_init(void) {
    /* Perform any initialisation you require here */
}

static size_t sos_debug_print(const void *vData, size_t count) {
    size_t i;
    const char *realdata = vData;
    for (i = 0; i < count; i++)
        seL4_DebugPutChar(realdata[i]);
    return count;
}

size_t sos_serial_write(const seL4_Word *data, size_t len) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, len + 1);
    seL4_SetTag(tag);
    seL4_SetMR(0, 2); // syscall 2 is what our protocol will use to write things

    size_t i;
    for (i = 0; i <= len / 4; i++) {
        seL4_SetMR(i + 1, data[i]);
    }

    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    return len;
}

size_t min(size_t a, size_t b) {
    return a < b ? a : b;
}

size_t sos_write(void *vData, size_t count) {
    size_t written = 0;
    size_t chunk_size = (seL4_MsgMaxLength - 1) * sizeof(seL4_Word);
    while (written < count) {
        size_t len = min(count - written, chunk_size);
        sos_debug_print(vData + written, len);
        written += sos_serial_write(vData + written, len);
    }

    return written;
}

size_t sos_read(void *vData, size_t count) {
    //implement this to use your syscall
    return 0;
}

