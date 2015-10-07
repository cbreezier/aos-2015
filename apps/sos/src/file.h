#ifndef _FILE_H_
#define _FILE_H_

#include <nfs/nfs.h>
#include <limits.h>
#include <sync/mutex.h>
#include <stdint.h>
#include <stdbool.h>
#include "proc.h"

#define FILES_PER_DIR 32

/* file modes */
#define FM_EXEC  1
#define FM_WRITE 2
#define FM_READ  4
typedef int fmode_t;

/* stat file types */
#define ST_FILE 1   /* plain file */
#define ST_SPECIAL 2    /* special (console) file */
typedef int st_type_t;

typedef struct {
    st_type_t st_type;    /* file type */
    fmode_t   st_fmode;   /* access mode */
    unsigned  st_size;    /* file size in bytes */
    long      st_ctime;   /* file creation time (ms since booting) */
    long      st_atime;   /* file last access (open) time (ms since booting) */
} sos_stat_t;


struct file_t;

typedef unsigned int size_t;

typedef int (*read_type)(process_t *proc, fhandle_t *fh, uint32_t offset, void *dest, size_t nbytes);
typedef int (*write_type)(process_t *proc, fhandle_t *fh, uint32_t offset, void *src, size_t nbytes);

struct file_t {
    read_type read;
    write_type write;

    fhandle_t fh;

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

    fmode_t mode;

    int next_free;
};

void open_files_init(void);

#endif /* _FILE_H_ */
