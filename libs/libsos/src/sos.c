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
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>

#include <sel4/sel4.h>
#include <sel4/types.h>
#include <sel4/constants.h>
#include <syscall.h>

#define SYSCALL_ENDPOINT_SLOT (1)

int sos_sys_null(void) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 1);
    seL4_SetTag(tag);
    seL4_SetMR(0, 0);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    return 0;
}

static int mode_to_sos(fmode_t mode) {
    int ret = 0;
    switch (mode) {
        case O_RDONLY:
            ret |= FM_READ;
            break;

        case O_WRONLY:
            ret |= FM_WRITE;
            break;

        case O_RDWR:
            ret |= FM_READ;
            ret |= FM_WRITE;
        default:
            break;
    }
    return ret;
}

int sos_sys_open(const char *path, fmode_t mode) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 3);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_open);
    seL4_SetMR(1, (seL4_Word)path);
    seL4_SetMR(2, (seL4_Word)mode_to_sos(mode));
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return seL4_GetMR(1);
}

int sos_sys_close(int file) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_close);
    seL4_SetMR(1, (seL4_Word)file);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return 0;
}

int sos_sys_read(int file, char *buf, size_t nbyte) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 4);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_read);
    seL4_SetMR(1, (seL4_Word)file);
    seL4_SetMR(2, (seL4_Word)buf);
    seL4_SetMR(3, (seL4_Word)nbyte);

    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    int ret = seL4_GetMR(1);
    if (err) {
        return -err;
    }
    return ret;
}

int sos_sys_write(int file, const char *buf, size_t nbyte) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 4);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_write);
    seL4_SetMR(1, (seL4_Word)file);
    seL4_SetMR(2, (seL4_Word)buf);
    seL4_SetMR(3, (seL4_Word)nbyte);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return seL4_GetMR(1);
}

void sos_sys_usleep(int usec) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_nanosleep);
    seL4_SetMR(1, (seL4_Word)usec);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
}

int64_t sos_sys_time_stamp(void) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 1);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_clock_gettime);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    int64_t timestamp = 0ll;
    timestamp |= seL4_GetMR(1);
    timestamp |= ((int64_t)seL4_GetMR(2) << 32);
    return timestamp;
}

int sos_getdirent(int pos, char *name, size_t nbyte) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 4);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_getdents);
    seL4_SetMR(1, (seL4_Word)pos);
    seL4_SetMR(2, (seL4_Word)name);
    seL4_SetMR(3, (seL4_Word)nbyte);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return seL4_GetMR(1);

}
/* Reads name of entry "pos" in directory into "name", max "nbyte" bytes.
 * Returns number of bytes returned, zero if "pos" is next free entry,
 * -1 if error (non-existent entry).
 */

int sos_stat(const char *path, sos_stat_t *buf) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 3);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_stat);
    seL4_SetMR(1, (seL4_Word)path);
    seL4_SetMR(2, (seL4_Word)buf);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return 0;
}
/* Returns information about file "path" through "buf".
 * Returns 0 if successful, -1 otherwise (invalid name).
 */

pid_t sos_process_create(const char *path) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_execve);
    seL4_SetMR(1, (seL4_Word)path);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return seL4_GetMR(1);
}
/* Create a new process running the executable image "path".
 * Returns ID of new process, -1 if error (non-executable image, nonexisting
 * file).
 */

int sos_process_delete(pid_t pid) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_kill);
    seL4_SetMR(1, (seL4_Word)pid);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return 0;
}
/* Delete process (and close all its file descriptors).
 * Returns 0 if successful, -1 otherwise (invalid process).
 */

pid_t sos_my_id(void) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 1);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_getpid);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    return seL4_GetMR(1);
}
/* Returns ID of caller's process. */

int sos_process_status(sos_process_t *processes, unsigned max) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 3);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_ustat);
    seL4_SetMR(1, (seL4_Word)processes);
    seL4_SetMR(2, (seL4_Word)max);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return seL4_GetMR(1);
}
/* Returns through "processes" status of active processes (at most "max"),
 * returns number of process descriptors actually returned.
 */

pid_t sos_process_wait(pid_t pid) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 2);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_waitid);
    seL4_SetMR(1, (seL4_Word)pid);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return seL4_GetMR(1);
}
/* Wait for process "pid" to exit. If "pid" is -1, wait for any process
 * to exit. Returns the pid of the process which exited.
 */

void sos_process_exit() {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 1);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_exit);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    assert(!"should never get here");
    while(true);
}

// static size_t sos_debug_print(const void *vData, size_t count) {
//     size_t i;
//     const char *realdata = vData;
//     for (i = 0; i < count; i++)
//         seL4_DebugPutChar(realdata[i]);
//     return count;
// }

size_t sos_serial_write(const seL4_Word *data, size_t len) {
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, (len+3)/4 + 2);
    seL4_SetTag(tag);
    seL4_SetMR(0, 2); // syscall 2 is what our protocol will use to write things
    seL4_SetMR(1, len);

    size_t i;
    for (i = 0; i <= len / 4; i++) {
        seL4_SetMR(i + 2, data[i]);
    }
    
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    return len;
}

size_t min(size_t a, size_t b) {
    return a < b ? a : b;
}

size_t sos_write(void *vData, size_t count) {
    size_t written = 0;
    size_t chunk_size = (seL4_MsgMaxLength - 2) * sizeof(seL4_Word);
    while (written < count) {
        size_t len = min(count - written, chunk_size);
//        sos_debug_print(vData + written, len);
//        written += len;
        written += sos_serial_write(vData + written, len);
    }

    return written;
}

size_t sos_read(void *vData, size_t count) {
    //implement this to use your syscall
    return 0;
}

