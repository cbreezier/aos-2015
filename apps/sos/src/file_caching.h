#ifndef _FILE_CACHING_H_
#define _FILE_CACHING_H_

#include <stdlib.h>
#include <nfs/nfs.h>
#include <limits.h>
#include <clock/clock.h>
#include "nfs_sync.h"
#include "proc.h"

struct file_entry;

struct vfs_cache_entry {
    /* Page sized */
    void *data;

    bool referenced;
    bool pinned;
    bool dirty; 

    struct file_t *file_obj;
    uint32_t nbytes; 
    uint32_t offset;

    /* Next entry in the file objs list */
    struct vfs_cache_entry *file_obj_next;
};

void vfs_cache_init();

int vfs_cache_read(process_t *proc, struct file_t *fe, uint32_t offset, void *usr_buf, size_t nbytes);

int vfs_cache_write(process_t *proc, struct file_t *fe, uint32_t offset, void *usr_buf, size_t nbytes);

int vfs_cache_lookup(char *path, fhandle_t *ret_fh, fattr_t *ret_fattr);

void vfs_cache_clear_file(struct file_t *fe);

#endif /* _FILE_CACHING_H_ */
