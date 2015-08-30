#include <console.h>
#include <sys/panic.h>
#include <sync/mutex.h>

#define MAX_BUFF_SIZE PAGE_SIZE

char buf[MAX_BUFF_SIZE];
uint32_t buf_pos;
uint32_t buf_size;

uint32_t reader_required_bytes;
bool has_notified;
sync_mutex_t serial_lock;

/* More of a notifying semaphore */
sync_mutex_t has_bytes_lock;

void serial_callback_handler(struct serial *serial, char c) {
    sync_acquire(serial_lock);
    if (buf_size >= MAX_BUFF_SIZE) {
        sync_release(serial_lock);
        /* Dropped chars */
        return;
    }
    uint32_t end = buf_pos + buf_size;
    if (end >= MAX_BUFF_SIZE) {
        end -= MAX_BUFF_SIZE;
    }
    buf[end] = c;

    ++buf_size;
    if ((c == '\n' || buf_size >= reader_required_bytes) 
         && reader_required_bytes != 0
         && !has_notified) {
        has_notified = true;
        sync_release(has_bytes_lock);
    }
    sync_release(serial_lock);

}

void console_init() {
    serial = serial_init();
    conditional_panic(!serial, "Cannot initialise serial device");
    int err = serial_register_handler(serial, serial_callback_handler);
    conditional_panic(err, "Cannot initialise callback handler - serial device");

    serial_lock = sync_create_mutex();
    conditional_panic(!serial_lock, "Cannot initialise serial device - serial lock");

    has_bytes_lock = sync_create_mutex();
    conditional_panic(!has_bytes_lock, "Cannot initialise serial device - has bytes lock");
    sync_acquire(has_bytes_lock);
    has_bytes_lock->holder = 0;
    has_notified = true;

    buf_pos = 0;
}

static int min(int a, int b) {
    return a < b ? a : b;
}

static int read_buf(void *dest, size_t nbytes, bool *read_newline) {
    int num_read = 0;
    sync_acquire(serial_lock);
    assert(buf_size >= 1 && "Not enough bytes to read from");
    for (size_t i = 0; i < nbytes; ++i, ++buf_pos, --buf_size) {
        if (buf_pos >= MAX_BUFF_SIZE) {
            buf_pos -= MAX_BUFF_SIZE;
        }
        ((char*)dest)[i] = buf[buf_pos];
        num_read++;
        if (buf[buf_pos] == '\n') {
            *read_newline = true;
            break;
        }
    }
    reader_required_bytes = 0;
    sync_release(serial_lock);
    return num_read;
}

int console_read(struct file_t *file, uint32_t offset, void *dest, size_t nbytes) {
    size_t nbytes_left = nbytes;
    bool read_newline = false;
    while (nbytes_left > 0 && !read_newline) {
        if (buf_size < nbytes_left) {
            size_t to_copy = min(nbytes_left, MAX_BUFF_SIZE);
            reader_required_bytes = to_copy;
            has_notified = false;
            sync_acquire(has_bytes_lock);
            
            nbytes_left -= read_buf(dest, to_copy, &read_newline);

            has_bytes_lock->holder = 0;
        } else {
            nbytes_left -= read_buf(dest, nbytes_left, &read_newline);
        }
    }
    printf("nbytes = %u, nbytes_left = %u\n", nbytes, nbytes_left);         
    return nbytes - nbytes_left;
}

int console_write(struct file_t *file, uint32_t offset, void *src, size_t nbytes) {
    return serial_send(serial, (char *)src, (int)nbytes);
}
