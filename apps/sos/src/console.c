#include <console.h>
#include <sys/panic.h>
#include <sync/mutex.h>
#include "copy.h"
#include "thread.h"
#include "frametable.h"

#define MAX_BUFF_SIZE PAGE_SIZE

char buf[MAX_BUFF_SIZE];
uint32_t buf_pos;
uint32_t buf_size;

uint32_t reader_required_bytes;
bool has_notified;
sync_mutex_t read_serial_lock;
sync_mutex_t write_serial_lock;

seL4_CPtr notify_async_ep;

void serial_callback_handler(struct serial *serial, char c) {
    sync_acquire(read_serial_lock);

    if (buf_size < MAX_BUFF_SIZE) {
        uint32_t end = buf_pos + buf_size;
        if (end >= MAX_BUFF_SIZE) {
            end -= MAX_BUFF_SIZE;
        }
        buf[end] = c;

        ++buf_size;
    }

    if ((c == '\n'/* || c == '\04' */|| buf_size >= reader_required_bytes) 
         && reader_required_bytes != 0
         && !has_notified) {
        has_notified = true;
        seL4_Notify(notify_async_ep, 0);
    }
    sync_release(read_serial_lock);

}

void console_init() {
    serial = serial_init();
    conditional_panic(!serial, "Cannot initialise serial device");
    int err = serial_register_handler(serial, serial_callback_handler);
    conditional_panic(err, "Cannot initialise callback handler - serial device");

    read_serial_lock = sync_create_mutex();
    conditional_panic(!read_serial_lock, "Cannot initialise serial device - read serial lock");
    write_serial_lock = sync_create_mutex();
    conditional_panic(!write_serial_lock, "Cannot initialise serial device - write serial lock");

    has_notified = true;

    buf_pos = 0;
}

static int min(int a, int b) {
    return a < b ? a : b;
}

/*
 * dest is a user address
 * returns number of bytes read, -err if err
 */
static int read_buf(process_t *proc, void *dest, size_t nbytes, bool *read_newline) {
    int num_read = 0;
    sync_acquire(read_serial_lock);
    assert(buf_size >= 1 && "Not enough bytes to read from");

    seL4_Word svaddr = 0;
    size_t can_read = 0;
    int err = 0;
    size_t bytes_left = nbytes;
    for (size_t i = 0; i < nbytes; ++i, ++buf_pos, --buf_size, --can_read) {
        if (buf_pos >= MAX_BUFF_SIZE) {
            buf_pos -= MAX_BUFF_SIZE;
        }
        if (!can_read) {
            if (svaddr != 0) {
                frame_change_swappable(svaddr, true);
            }
            err = usr_buf_to_sos(proc, dest, bytes_left, &svaddr, &can_read);
            if (err) {
                sync_release(read_serial_lock);
                return -err;
            }
            assert(can_read && "Should be able to read after translating page");
        }

        ((char*)svaddr)[i] = buf[buf_pos];

        num_read++;
        bytes_left--;
        if (buf[buf_pos] == '\n') {
            *read_newline = true;
            ++buf_pos;
            --buf_size;
            break;
        }
    }
    if (svaddr != 0) {
        frame_change_swappable(svaddr, true);
    }

    reader_required_bytes = 0;
    sync_release(read_serial_lock);
    return num_read;
}

int console_read(process_t *proc, struct file_t *file, uint32_t offset, void *dest, size_t nbytes) {
    /* TODO M7: Lock around entire console read? enforce only 1 reader */
    if (!usr_buf_in_region(proc, dest, nbytes, NULL, NULL)) {
        return -EFAULT;
    }

    size_t nbytes_left = nbytes;
    bool read_newline = false;
    int err = 0;
    while (nbytes_left > 0 && !read_newline) {
        sync_acquire(read_serial_lock);
        if (buf_size < nbytes_left) {

            size_t to_copy = min(nbytes_left, MAX_BUFF_SIZE);
            reader_required_bytes = to_copy;
            has_notified = false;
            notify_async_ep = get_cur_thread()->wakeup_async_ep;
            sync_release(read_serial_lock);

            seL4_Wait(notify_async_ep, NULL);

            err = read_buf(proc, dest, to_copy, &read_newline);
            if (err < 0) {
                return err;
            }
            nbytes_left -= err; 

        } else {
            err = read_buf(proc, dest, nbytes_left, &read_newline);
            sync_release(read_serial_lock);
            if (err < 0) {
                return err;
            }
            nbytes_left -= err;
        }
    }
    return nbytes - nbytes_left;
}

/* src is a user address */
int console_write(process_t *proc, struct file_t *file, uint32_t offset, void *src, size_t nbytes) {
    bool region_r;
    if (!usr_buf_in_region(proc, src, nbytes, &region_r, NULL)) {
        return -EFAULT;
    }

    if (!region_r) {
        return -EACCES;
    }

    sync_acquire(write_serial_lock);
    int bytes_left = nbytes;
    int err = 0;
    seL4_Word svaddr = 0;
    size_t to_write = 0;
    while (bytes_left > 0) {
        err = usr_buf_to_sos(proc, src, (size_t) bytes_left, &svaddr, &to_write);
        if (err) {
            sync_release(write_serial_lock);
            return -err;
        }
        int written = serial_send(serial, (char *)svaddr, (int) to_write);
        frame_change_swappable(svaddr, true);

        bytes_left -= written;
    }
    sync_release(write_serial_lock);

    return nbytes;
}
