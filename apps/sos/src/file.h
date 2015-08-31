#ifndef _FILE_H_
#define _FILE_H_

#include <sel4/sel4.h>
#include <limits.h>
#include <sync/mutex.h>
#include <stdint.h>
#include <stdbool.h>

struct file_t;

typedef unsigned int size_t;

typedef int (*read_type)(struct file_t *file, uint32_t offset, void *dest, size_t nbytes);
typedef int (*write_type)(struct file_t *file, uint32_t offset, void *src, size_t nbytes);

struct file_t {
    /* Function Pointers
     
     */

    read_type read;
    write_type write;

    char name[NAME_MAX];
};

struct file_entry {
    uint32_t ref_count;

    struct file_t file_obj;

} open_files[OPEN_FILE_MAX];
sync_mutex_t open_files_lock;

struct fd_entry {
    bool used;
    uint32_t offset;
    size_t open_file_idx;

    int next_free;
};

void open_files_init(void);

#endif /* _FILE_H_ */
