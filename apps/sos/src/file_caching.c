#include <utils/page.h>
#include <sys/debug.h>
#include <sys/panic.h>
#include <string.h>
#include <clock/clock.h>
#include "file_caching.h"
#include "file.h"
#include "frametable.h"
#include "copy.h"

#define NUM_CACHE_ENTRIES 128
#define NUM_CACHE_DIR_ENTRIES 32

#define WRITE_TICK_TIME 30000000ull

#define FILE_CACHEING_VERBOSITY 5

struct vfs_cache_entry cache[NUM_CACHE_ENTRIES];

uint32_t _cur_cache_entry = 0;

sync_mutex_t cache_lock;

struct vfs_cache_dir_entry {
    fhandle_t fh;
    fattr_t fattr;

    char path[NAME_MAX];
} dir_cache[NUM_CACHE_DIR_ENTRIES];

sync_mutex_t dir_cache_lock;

/*
 * Go through every cache entry and write it out to disk
 * if it is dirty. This gets called every WRITE_TICK_TIME
 * nanoseconds and provides some guarantee of disk coherence
 */
static void write_tick(uint32_t id, void *data) {
    sync_acquire(cache_lock);
    /* Do flushing of pages stuff */
    for (int i = 0; i < NUM_CACHE_ENTRIES; ++i) {
        if (cache[i].dirty) {
            cache[i].pinned = true;
            
            /* Setting dirty to false here might cause a race condition
             * where data is written to the cache entry, causing dirty
             * to be set to true, and is also written out to disk.
             *
             * This causes extra writes to occur in this race condition,
             * but that is okay, as it will be incredibly uncommon, and
             * will not result in any lost data.
             */
            cache[i].dirty = false;
            sync_release(cache_lock);

            sync_acquire(cache[i].file_obj->file_lock);
        
            nfs_sos_write_sync(cache[i].file_obj->fh, cache[i].offset, cache[i].data, cache[i].nbytes);

            sync_release(cache[i].file_obj->file_lock);

            sync_acquire(cache_lock);
            cache[i].pinned = false;
        }
    }
    sync_release(cache_lock);

    register_timer(WRITE_TICK_TIME, write_tick, NULL, timer_ep);
}

void vfs_cache_init() {
    for (int i = 0; i < NUM_CACHE_ENTRIES; ++i) {
        cache[i].data = (void*)frame_alloc_sos(false);
        conditional_panic(!cache[i].data, "Not enough memory for vfs cache");
        cache[i].referenced = false;
        cache[i].pinned = false;
        cache[i].dirty = false;

        cache[i].file_obj = NULL;
        cache[i].nbytes = 0;
        cache[i].file_obj_next = NULL;
        cache[i].offset = 0;
    }
    for (int i = 0; i < NUM_CACHE_DIR_ENTRIES; ++i) {
        dir_cache[i].path[0] = '\0';
    }
    cache_lock = sync_create_mutex();
    conditional_panic(!cache_lock, "Cannot create cache lock");

    dir_cache_lock = sync_create_mutex();
    conditional_panic(!dir_cache_lock, "Cannot create dir cache lock");

    register_timer(WRITE_TICK_TIME, write_tick, NULL, timer_ep);
}

/* MUST BE HOLDING CACHE_LOCK BEFORE CALLING THIS */
static int cache_out() {
    int num_loops = 0;
    /* Find first unreferenced cache entry to reuse - clock algorithm */
    while (cache[_cur_cache_entry].referenced || cache[_cur_cache_entry].pinned) {
        cache[_cur_cache_entry].referenced = false;   
        
        _cur_cache_entry++;
        if (_cur_cache_entry >= NUM_CACHE_ENTRIES) {
            _cur_cache_entry = 0;   
        }
        num_loops++;
        /* Every entry must be pinned */
        conditional_panic(num_loops > NUM_CACHE_ENTRIES*2 + 1, "All vfs cache entries are pinned");
    }

    /* Write out page to disk if dirty */
    if (cache[_cur_cache_entry].dirty) {
        cache[_cur_cache_entry].pinned = true;

        sync_release(cache_lock);

        struct vfs_cache_entry *entry = &cache[_cur_cache_entry];

        int nwritten = nfs_sos_write_sync(entry->file_obj->fh, entry->offset, entry->data, entry->nbytes);
        conditional_panic(nwritten != entry->nbytes, "Writing out cache entry failed");

        sync_acquire(cache_lock);

        cache[_cur_cache_entry].pinned = false;

        memset(cache[_cur_cache_entry].data, 0, PAGE_SIZE);
    }

    /* Remove the cache entry from the list of cache entries on a file */
    struct file_t *file_obj = cache[_cur_cache_entry].file_obj;
    if (file_obj != NULL) {
        struct vfs_cache_entry *prev = NULL;
        bool found = false;

        for (struct vfs_cache_entry *cur = file_obj->cache_entry_head; cur != NULL; cur = cur->file_obj_next) {

            if (cur->offset == cache[_cur_cache_entry].offset) {
                found = true;
                if (prev == NULL) {
                    file_obj->cache_entry_head = cur->file_obj_next;
                } else {
                    prev->file_obj_next = cur->file_obj_next;
                }
                cur->file_obj_next = NULL;
                break;
            }

            prev = cur;
        }
        conditional_panic(!found, "file entries and vfs cache are not consistent");
    }

    int ret = _cur_cache_entry;
    _cur_cache_entry++;
    if (_cur_cache_entry >= NUM_CACHE_ENTRIES) {
        _cur_cache_entry = 0;
    }
    return ret;
}

static int min(int a, int b) {
    return a < b ? a : b;
}

static int max(int a, int b) {
    return a > b ? a : b;
}

int vfs_cache_read(process_t *proc, struct file_t *fe, uint32_t offset, void *usr_buf, size_t nbytes) {
    sync_acquire(fe->file_lock);
    int nread = 0;

    uint32_t aligned_offset = PAGE_ALIGN(offset, PAGE_SIZE);
    offset -= aligned_offset;

    sync_acquire(cache_lock);

    /*
     * Cache entries associated with file are stored sorted by
     * base offset. Find the first offset which matches or is just
     * past where we want to start reading from.
     *
     * This is incremented as we read pages.
     */
    struct vfs_cache_entry *cur = fe->cache_entry_head;
    struct vfs_cache_entry *prev = NULL;
    while (cur != NULL && cur->offset < aligned_offset) {
        prev = cur;
        cur = cur->file_obj_next;
    }

    if (cur) cur->pinned = true;
    if (prev) prev->pinned = true;

    while (nbytes > 0) {
        /* Found a cache hit */
        if (cur != NULL && cur->offset == aligned_offset) {
            if (cur->nbytes <= offset) {
                /* Trying to read past the end of the file */
                break;
            }
            cur->pinned = true;
            sync_release(cache_lock);

            size_t to_copy = min(PAGE_SIZE - offset, cur->nbytes - offset);
            to_copy = min(to_copy, nbytes);

            int err = copyout(proc, usr_buf, cur->data + offset, to_copy);
            if (err) {
                sync_acquire(cache_lock);
                cur->pinned = false;
                sync_release(cache_lock);
                sync_release(fe->file_lock);
                return -err;
            }

            nread += to_copy;
            nbytes -= to_copy;
            usr_buf += to_copy;
            aligned_offset += PAGE_SIZE;
            offset = 0;

            sync_acquire(cache_lock);
            cur->pinned = false;
            cur->referenced = true;
        
            if (prev) prev->pinned = false;
            prev = cur;
            cur = cur->file_obj_next;
            if (cur) cur->pinned = true;
        } else {
            /* No cache hit, get ready to replace and read into a cache entry */
            int entry = cache_out();
            conditional_panic(entry < 0 || entry >= NUM_CACHE_ENTRIES, "Invalid cache entry (vfs_cache_read)");

            cache[entry].pinned = true;
            
            sync_release(cache_lock);

            /* Actual read from disk */
            int nfs_nread = nfs_sos_read_sync(fe->fh, aligned_offset, cache[entry].data, PAGE_SIZE);
            if (nfs_nread < 0) {
                sync_acquire(cache_lock);
                cache[entry].pinned = false;
                sync_release(cache_lock);
                sync_release(fe->file_lock);
                return nfs_nread;
            }


            size_t to_read = min(PAGE_SIZE, PAGE_SIZE - offset);
            to_read = min(to_read, nbytes);
            to_read = min(to_read, nfs_nread);

            /* Copyout to user space */
            int err = copyout(proc, usr_buf, cache[entry].data + offset, to_read);
            if (err) {
                sync_acquire(cache_lock);
                cache[entry].pinned = false;
                sync_release(cache_lock);
                sync_release(fe->file_lock);
                return -err;
            }

            nread += to_read;
            nbytes -= to_read;
            usr_buf += to_read;
            offset = 0;

            sync_acquire(cache_lock);

            /* Set cache entry data */
            cache[entry].pinned = false;
            cache[entry].referenced = true;
            cache[entry].dirty = false;
            cache[entry].file_obj = fe;
            cache[entry].nbytes = nfs_nread;
            cache[entry].offset = aligned_offset;

            aligned_offset += PAGE_SIZE;

            /* Add new cache entry to list of cache entries on this file */
            conditional_panic(prev == cur && prev != NULL, "something something has grone wrong");
            if (prev == NULL) {
                fe->cache_entry_head = &cache[entry];
            } else {
                prev->file_obj_next = &cache[entry];
            }
            cache[entry].file_obj_next = cur;

            if (nfs_nread < PAGE_SIZE) {
                break;
            }

            if (prev) prev->pinned = false;
            prev = &cache[entry];
            if (prev) prev->pinned = true;
        }
    }

    if (prev) prev->pinned = false;
    if (cur) cur->pinned = false;

    sync_release(cache_lock);
    sync_release(fe->file_lock);

    return nread;
}

/*
 * djb2 by Dan Bernstein
 * http://www.cse.yorku.ca/~oz/hash.html
 */
static int hash_string(char *path) {
    unsigned long hash = 5381;

    for (int c = (*path++); c != 0; c = (*path++)) {
        hash = ((hash << 5) + hash) + c;   
    }

    return hash % NUM_CACHE_DIR_ENTRIES;
}

int vfs_cache_write(process_t *proc, struct file_t *fe, uint32_t offset, void *usr_buf, size_t nbytes) {
    int nwritten = 0;
    uint32_t saved_offset = offset;

    sync_acquire(fe->file_lock);

    uint32_t aligned_offset = PAGE_ALIGN(offset, PAGE_SIZE);
    offset -= aligned_offset;

    sync_acquire(cache_lock);

    struct vfs_cache_entry *cur = fe->cache_entry_head;
    while (cur != NULL && cur->offset < aligned_offset) {
        cur = cur->file_obj_next;
    }

    if (cur) cur->pinned = true;
   
    while (nbytes > 0) {
        if (cur != NULL && cur->offset == aligned_offset) {
            /* Cache entry exists, simply overwrite whatever is necessary */
            size_t to_write = min(PAGE_SIZE - offset, nbytes);            
            size_t end_loc = offset + to_write;

            cur->pinned = true;
            sync_release(cache_lock);

            int err = copyin(proc, cur->data + offset, usr_buf, to_write);
            if (err) {
                sync_acquire(cache_lock);
                cur->pinned = false;
                sync_release(cache_lock);
                sync_release(fe->file_lock);
                return -err;
            }

            /* Update number of bytes in this PAGE_SIZE data block */
            cur->nbytes = max(offset + to_write, cur->nbytes);

            nwritten += to_write;
            nbytes -= to_write;
            usr_buf += to_write;
            offset = 0;
            aligned_offset += PAGE_SIZE;

            sync_acquire(cache_lock);
            
            cur->pinned = false;
            cur->nbytes = max(cur->nbytes, end_loc);
            cur->dirty = true;
            cur->referenced = true;

            cur = cur->file_obj_next;
            if (cur) cur->pinned = true;
        } else {
            sync_release(cache_lock);

            /* Cache entry does not exist. Write through */
            size_t to_write = min(PAGE_SIZE - offset, nbytes);
            int nfs_nwritten = nfs_write_sync(proc, &fe->fh, aligned_offset + offset, usr_buf, to_write);
            if (nfs_nwritten < 0) {
                sync_release(fe->file_lock);
                return nfs_nwritten; 
            }
            conditional_panic(nfs_nwritten != to_write, "nfs write sync failed in vfs_cache_write");

            nwritten += to_write;
            usr_buf += to_write;
            nbytes -= to_write;
            offset = 0;
            aligned_offset += PAGE_SIZE;

            sync_acquire(cache_lock);
        }
    }

    if (cur) cur->pinned = false;

    sync_release(cache_lock);

    sync_release(fe->file_lock);

    /* Update file size if we have written more */
    int hash = hash_string(fe->name);

    sync_acquire(dir_cache_lock);
    if (strcmp(fe->name, dir_cache[hash].path) == 0) {
        dir_cache[hash].fattr.size = max(dir_cache[hash].fattr.size, saved_offset + nwritten);
    }
    sync_release(dir_cache_lock);

    return nwritten;
}

void vfs_cache_clear_file(struct file_t *fe) {
    sync_acquire(fe->file_lock);
    sync_acquire(cache_lock);

    struct vfs_cache_entry *next_entry = NULL;
    for (struct vfs_cache_entry *cur = fe->cache_entry_head; cur != NULL; ) {
        /* Write out to disk if dirty */
        if (cur->dirty) {
            cur->pinned = true;

            sync_release(cache_lock);
            nfs_sos_write_sync(fe->fh, cur->offset, cur->data, cur->nbytes);
            sync_acquire(cache_lock);

            cur->dirty = false;
            cur->pinned = false;
        }
        cur->referenced = false;
        cur->file_obj = NULL;
        next_entry = cur->file_obj_next;
        cur->file_obj_next = NULL;
        memset(cur->data, 0, PAGE_SIZE);

        cur = next_entry;
    }
    fe->cache_entry_head = NULL;

    sync_release(cache_lock);
    sync_release(fe->file_lock);
}

int vfs_cache_lookup(char *path, fhandle_t *ret_fh, fattr_t *ret_fattr) {
    int hash = hash_string(path);

    sync_acquire(dir_cache_lock);
    if (strcmp(dir_cache[hash].path, path) != 0) {
        /* Overwrites previous hash entry if it exists*/
        int err = nfs_lookup_sync(path, &dir_cache[hash].fh, &dir_cache[hash].fattr);
        if (err) {
            return err;
        }
        strncpy(dir_cache[hash].path, path, NAME_MAX);
        dir_cache[hash].path[NAME_MAX - 1] = '\0';
    }

    if (ret_fh != NULL) *ret_fh = dir_cache[hash].fh;
    if (ret_fattr != NULL) *ret_fattr = dir_cache[hash].fattr;
    sync_release(dir_cache_lock);

    return 0;
}
