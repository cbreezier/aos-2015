#include <console.h>
#include <sys/panic.h>
#include <sync/mutex.h>
#include "copy.h"
#include "thread.h"
#include "frametable.h"
#include "nfs_sync.h"

#define MAX_BUFF_SIZE PAGE_SIZE

char buf[MAX_BUFF_SIZE];
uint32_t buf_pos;
uint32_t buf_size;

uint32_t reader_required_bytes;
bool has_notified;
sync_mutex_t read_serial_lock;
sync_mutex_t write_serial_lock;
sync_mutex_t one_reader_lock;

seL4_CPtr notify_async_ep;

size_t num_newlines;

void serial_callback_handler(struct serial *serial, char c) {
    sync_release(network_lock);
    sync_acquire(read_serial_lock);

    if (buf_size < MAX_BUFF_SIZE) {
        uint32_t end = buf_pos + buf_size;
        if (end >= MAX_BUFF_SIZE) {
            end -= MAX_BUFF_SIZE;
        }
        buf[end] = c;

        ++buf_size;
    }

    if (c == '\n') {
        num_newlines++;
    }

    if ((num_newlines > 0 || buf_size >= reader_required_bytes) 
         && reader_required_bytes != 0
         && !has_notified) {
        has_notified = true;
        seL4_Notify(notify_async_ep, 0);
    }
    sync_release(read_serial_lock);
    sync_acquire(network_lock);
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
    one_reader_lock = sync_create_mutex();
    conditional_panic(!one_reader_lock, "Cannot initialise serial device - one reader lock");


    has_notified = true;

    buf_pos = 0;
    num_newlines = 0;
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
            sync_release(read_serial_lock);
            if (svaddr != 0) {
                frame_change_swappable(svaddr, true);
            }
            err = usr_buf_to_sos(proc, dest, bytes_left, &svaddr, &can_read);
            if (err) {
                return -err;
            }
            assert(can_read && "Should be able to read after translating page");
            sync_acquire(read_serial_lock);
        }

        ((char*)svaddr)[i] = buf[buf_pos];

        num_read++;
        bytes_left--;
        if (buf[buf_pos] == '\n') {
            *read_newline = true;
            ++buf_pos;
            --buf_size;
            --num_newlines;
            break;
        }
    }
    reader_required_bytes = 0;
    sync_release(read_serial_lock);

    if (svaddr != 0) {
        frame_change_swappable(svaddr, true);
    }
    return num_read;
}

int console_read(process_t *proc, struct file_t *fe, uint32_t offset, void *dest, size_t nbytes) {
    if (!usr_buf_in_region(proc, dest, nbytes, NULL, NULL)) {
        return -EFAULT;
    }

    sync_acquire(one_reader_lock);
    size_t nbytes_left = nbytes;
    bool read_newline = false;
    int err = 0;
    while (nbytes_left > 0 && !read_newline) {
        sync_acquire(read_serial_lock);
        if (buf_size < nbytes_left && num_newlines == 0) {

            size_t to_copy = min(nbytes_left, MAX_BUFF_SIZE);
            reader_required_bytes = to_copy;
            has_notified = false;
            notify_async_ep = get_cur_thread()->wakeup_async_ep;
            sync_release(read_serial_lock);

            seL4_Wait(notify_async_ep, NULL);

            err = read_buf(proc, dest, to_copy, &read_newline);
            if (err < 0) {
                sync_release(one_reader_lock);
                return err;
            }
            nbytes_left -= err; 

        } else {
            sync_release(read_serial_lock);
            err = read_buf(proc, dest, nbytes_left, &read_newline);
            if (err < 0) {
                sync_release(one_reader_lock);
                return err;
            }
            nbytes_left -= err;
        }
    }
    sync_release(one_reader_lock);
    return nbytes - nbytes_left;
}

/* src is a user address */
int console_write(process_t *proc, struct file_t *fe, uint32_t offset, void *src, size_t nbytes) {
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
        sync_acquire(network_lock);
        int written = serial_send(serial, (char *)svaddr, (int) to_write);
        sync_release(network_lock);
        frame_change_swappable(svaddr, true);

        bytes_left -= written;
    }
    sync_release(write_serial_lock);

    return nbytes;
}
