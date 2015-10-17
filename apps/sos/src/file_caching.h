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
    /* Page sized cached data */
    void *data;

    /* Used for second chance clock replacement */
    bool referenced;
    /* Cannot reused */
    bool pinned;
    /* Cache entry has been written to and must be written to disk */
    bool dirty; 

    /* File object that it references */
    struct file_t *file_obj;
    /* Number of bytes - usually PAGE_SIZE except end of file */
    uint32_t nbytes; 
    /* PAGE_SIZE'd multiple - base offset into the file */
    uint32_t offset;

    /* Next entry in the file objs list */
    struct vfs_cache_entry *file_obj_next;
};

/* Initialise locks and set entry bits to false */
void vfs_cache_init();

/*
 * Read from a cache entry if it exists - otherwise calls nfs_read_sync and caches
 * each page of data into a free cache entry. If no entry exists, reuses the first
 * unreferenced cache entry (writing out its data if dirty).
 *
 * Uses copyout for cache reads and maps in user pages as needed.
 *
 * Returns number of bytes read, or -error
 */
int vfs_cache_read(process_t *proc, struct file_t *fe, uint32_t offset, void *usr_buf, size_t nbytes);

/*
 * Invalidates directory cache entry.
 *
 * For each page of data to write out, if a cache entry for that portion already
 * exists then the cache entry is modified and dirty bit is set. Otherwise writes
 * through to disk.
 *
 * Returns number of bytes written, or -error
 */
int vfs_cache_write(process_t *proc, struct file_t *fe, uint32_t offset, void *usr_buf, size_t nbytes);

/*
 * Cache layer before nfs_lookup_sync. Hash lookup on filename to see if cache
 * entry exists, and reads from that if it does
 *
 * Returns 0 on success and err otherwise
 */
int vfs_cache_lookup(char *path, fhandle_t *ret_fh, fattr_t *ret_fattr);

/*
 * Releases all cache entries associated with the given file object fte
 * and writes out any dirty cache entries.
 */
void vfs_cache_clear_file(struct file_t *fe);

#endif /* _FILE_CACHING_H_ */
