#ifndef _FILE_H_
#define _FILE_H_

#include <nfs/nfs.h>
#include <limits.h>
#include <sync/mutex.h>
#include <stdint.h>
#include <stdbool.h>
#include "proc.h"
#include "file_caching.h"

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

/*
 * Type signatures for all read and write functions on all file objects,
 * notably just nfs files and special console file.
 */
typedef int (*read_type)(process_t *proc, struct file_t *fe, uint32_t offset, void *dest, size_t nbytes);
typedef int (*write_type)(process_t *proc, struct file_t *fe, uint32_t offset, void *src, size_t nbytes);

/*
 * Abstract representation of a file, containing
 * the functions used to interface with it, a file handle and a name
 */
struct file_t {
    read_type read;
    write_type write;

    struct vfs_cache_entry *cache_entry_head; 

    fhandle_t fh;

    sync_mutex_t file_lock;

    char name[NAME_MAX];
};

/*
 * Global open file table which is pointed to by all file descriptor
 * entries in every process
 */
struct file_entry {
    /* Number of file descriptors open on this file */
    uint32_t ref_count;

    /* The file this entry represents */
    struct file_t file_obj;

} open_files[OPEN_FILE_MAX];
sync_mutex_t open_files_lock;

/*
 * Per process file descriptor entry, tracking which
 * open file table index it points to and its own personal
 * offset within the file
 *
 * Implemented as an array-linked-list within each process
 */
struct fd_entry {
    /* This particular file descriptor is in use */
    bool used;
    /* Offset within the file used for reads and writes */
    uint32_t offset;
    /* Open file table index this file descriptor points to */
    size_t open_file_idx;

    /* Permissions of the opened file */
    fmode_t mode;

    /* Next free descriptor, linked list style */
    int next_free;
};

/* Initialise open file table ref_counts and locks */
void open_files_init(void);

#endif /* _FILE_H_ */
