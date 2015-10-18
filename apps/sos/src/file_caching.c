#include <utils/page.h>
#include <sys/debug.h>
#include <sys/panic.h>
#include <string.h>
#include "file_caching.h"
#include "file.h"
#include "frametable.h"
#include "copy.h"

#define NUM_CACHE_ENTRIES 128
#define NUM_CACHE_DIR_ENTRIES 32

#define WRITE_TICK_TIME 30000000ull

#define FILE_CACHEING_VERBOSITY 5

struct vfs_cache_entry cache[NUM_CACHE_ENTRIES];

struct timer_list_node write_tick_node;

uint32_t _cur_cache_entry = 0;

sync_mutex_t cache_lock;

struct vfs_cache_dir_entry {
    fhandle_t fh;
    fattr_t fattr;

    char path[NAME_MAX];
} dir_cache[NUM_CACHE_DIR_ENTRIES];

sync_mutex_t dir_cache_lock;

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

    register_timer(WRITE_TICK_TIME, write_tick, NULL, &write_tick_node);
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

    register_timer(WRITE_TICK_TIME, write_tick, NULL, &write_tick_node);
}

/* MUST BE HOLDING CACHE_LOCK BEFORE CALLING THIS */
static int cache_out() {
    int num_loops = 0;
    while (cache[_cur_cache_entry].referenced || cache[_cur_cache_entry].pinned) {
        dprintf(FILE_CACHEING_VERBOSITY, "cache out loop %d\n", num_loops);
        cache[_cur_cache_entry].referenced = false;   
        
        _cur_cache_entry++;
        if (_cur_cache_entry >= NUM_CACHE_ENTRIES) {
            _cur_cache_entry = 0;   
        }
        num_loops++;
        /* Every entry must be pinned */
        conditional_panic(num_loops > NUM_CACHE_ENTRIES*2 + 1, "All vfs cache entries are pinned");
    }

    dprintf(FILE_CACHEING_VERBOSITY, "cache out check dirty\n");
    if (cache[_cur_cache_entry].dirty) {
        dprintf(FILE_CACHEING_VERBOSITY, "cache out dirty\n");
        cache[_cur_cache_entry].pinned = true;

        dprintf(FILE_CACHEING_VERBOSITY, "cache out releasing\n");
        sync_release(cache_lock);
        dprintf(FILE_CACHEING_VERBOSITY, "cache out released\n");

        struct vfs_cache_entry *entry = &cache[_cur_cache_entry];

        dprintf(FILE_CACHEING_VERBOSITY, "cache out before nfs\n");
        int nwritten = nfs_sos_write_sync(entry->file_obj->fh, entry->offset, entry->data, entry->nbytes);
        dprintf(FILE_CACHEING_VERBOSITY, "cache out after nfs\n");
        conditional_panic(nwritten != entry->nbytes, "Writing out cache entry failed");

        dprintf(FILE_CACHEING_VERBOSITY, "cache out before lock\n");
        sync_acquire(cache_lock);
        dprintf(FILE_CACHEING_VERBOSITY, "cache out after lock\n");

        cache[_cur_cache_entry].pinned = false;

        memset(cache[_cur_cache_entry].data, 0, PAGE_SIZE);
    }
    dprintf(FILE_CACHEING_VERBOSITY, "cache out done dirty\n");

    struct file_t *file_obj = cache[_cur_cache_entry].file_obj;
    dprintf(FILE_CACHEING_VERBOSITY, "cache out check file_obj\n");
    if (file_obj != NULL) {
        dprintf(FILE_CACHEING_VERBOSITY, "cache out file_obj not null\n");
        struct vfs_cache_entry *prev = NULL;
        bool found = false;

        for (struct vfs_cache_entry *cur = file_obj->cache_entry_head; cur != NULL; cur = cur->file_obj_next) {
            dprintf(FILE_CACHEING_VERBOSITY, "cache out for loop %p\n", cur);

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
    dprintf(FILE_CACHEING_VERBOSITY, "cache out check file_obj done\n");

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
    dprintf(FILE_CACHEING_VERBOSITY, "read file lock before %d\n", proc->pid);
    sync_acquire(fe->file_lock);
    dprintf(FILE_CACHEING_VERBOSITY, "read file lock after %d\n", proc->pid);
    int nread = 0;

    uint32_t aligned_offset = PAGE_ALIGN(offset, PAGE_SIZE);
    offset -= aligned_offset;

    dprintf(FILE_CACHEING_VERBOSITY, "read cache lock before %d\n", proc->pid);
    sync_acquire(cache_lock);
    dprintf(FILE_CACHEING_VERBOSITY, "read cache lock after %d\n", proc->pid);

    struct vfs_cache_entry *cur = fe->cache_entry_head;
    struct vfs_cache_entry *prev = NULL;
    while (cur != NULL && cur->offset < aligned_offset) {
        prev = cur;
        cur = cur->file_obj_next;
    }

    if (cur) cur->pinned = true;
    if (prev) prev->pinned = true;

    while (nbytes > 0) {
        dprintf(FILE_CACHEING_VERBOSITY, "wtf %d\n", proc->pid);
        if (cur != NULL && cur->offset == aligned_offset) {
            dprintf(FILE_CACHEING_VERBOSITY, "read if %d", proc->pid);
            if (cur->nbytes <= offset) {
                /* Trying to read past the end of the file */
                break;
            }
            cur->pinned = true;
            dprintf(FILE_CACHEING_VERBOSITY, "read if a %d\n", proc->pid);
            sync_release(cache_lock);
            dprintf(FILE_CACHEING_VERBOSITY, "read if b %d\n", proc->pid);

            size_t to_copy = min(PAGE_SIZE - offset, cur->nbytes - offset);
            dprintf(FILE_CACHEING_VERBOSITY, "read if c %d\n", proc->pid);
            to_copy = min(to_copy, nbytes);

            dprintf(FILE_CACHEING_VERBOSITY, "read copyout before %d\n", proc->pid);
            int err = copyout(proc, usr_buf, cur->data + offset, to_copy);
            dprintf(FILE_CACHEING_VERBOSITY, "read copyout after %d\n", proc->pid);
            if (err) {
                sync_acquire(cache_lock);
                cur->pinned = false;
                sync_release(cache_lock);
                sync_release(fe->file_lock);
                return -err;
            }

            dprintf(FILE_CACHEING_VERBOSITY, "read if d %d\n", proc->pid);
            nread += to_copy;
            nbytes -= to_copy;
            usr_buf += to_copy;
            aligned_offset += PAGE_SIZE;
            offset = 0;
            dprintf(FILE_CACHEING_VERBOSITY, "read if e %d\n", proc->pid);

            dprintf(FILE_CACHEING_VERBOSITY, "read re-acquire %d\n", proc->pid);
            sync_acquire(cache_lock);
            dprintf(FILE_CACHEING_VERBOSITY, "read re-acquire done %d\n", proc->pid);
            cur->pinned = false;
            cur->referenced = true;
        
            dprintf(FILE_CACHEING_VERBOSITY, "read if f %d\n", proc->pid);
            if (prev) prev->pinned = false;
            prev = cur;
            cur = cur->file_obj_next;
            if (cur) cur->pinned = true;
            dprintf(FILE_CACHEING_VERBOSITY, "read if g %d\n", proc->pid);
        } else {
            dprintf(FILE_CACHEING_VERBOSITY, "read else cache_out before %d\n", proc->pid);
            int entry = cache_out();
            dprintf(FILE_CACHEING_VERBOSITY, "read else cache_out after %d\n", proc->pid);
            conditional_panic(entry < 0 || entry >= NUM_CACHE_ENTRIES, "Invalid cache entry (vfs_cache_read)");

            cache[entry].pinned = true;
            
            sync_release(cache_lock);

            dprintf(FILE_CACHEING_VERBOSITY, "read nfs_read before %d\n", proc->pid);
            int nfs_nread = nfs_sos_read_sync(fe->fh, aligned_offset, cache[entry].data, PAGE_SIZE);
            dprintf(FILE_CACHEING_VERBOSITY, "read nfs_read after %d\n", proc->pid);
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

            dprintf(FILE_CACHEING_VERBOSITY, "read else re-acquire %d\n", proc->pid);
            sync_acquire(cache_lock);
            dprintf(FILE_CACHEING_VERBOSITY, "read else re-acquire done %d\n", proc->pid);

            cache[entry].pinned = false;
            cache[entry].referenced = true;
            cache[entry].dirty = false;
            cache[entry].file_obj = fe;
            cache[entry].nbytes = nfs_nread;
            cache[entry].offset = aligned_offset;

            aligned_offset += PAGE_SIZE;

            conditional_panic(prev == cur && prev != NULL, "something something has grone wrong");
            dprintf(FILE_CACHEING_VERBOSITY, "read else a %d\n", proc->pid);
            if (prev == NULL) {
                dprintf(FILE_CACHEING_VERBOSITY, "read else b %d\n", proc->pid);
                fe->cache_entry_head = &cache[entry];
            } else {
                dprintf(FILE_CACHEING_VERBOSITY, "read else c %d\n", proc->pid);
                prev->file_obj_next = &cache[entry];
            }
            dprintf(FILE_CACHEING_VERBOSITY, "read else d %d\n", proc->pid);
            cache[entry].file_obj_next = cur;
            dprintf(FILE_CACHEING_VERBOSITY, "read else e %d\n", proc->pid);

            if (nfs_nread < PAGE_SIZE) {
                dprintf(FILE_CACHEING_VERBOSITY, "read else f %d\n", proc->pid);
                break;
            }

            if (prev) prev->pinned = false;
            prev = &cache[entry];
            if (prev) prev->pinned = true;
            dprintf(FILE_CACHEING_VERBOSITY, "read else g %d\n", proc->pid);
        }
    }

    if (prev) prev->pinned = false;
    if (cur) cur->pinned = false;

    sync_release(cache_lock);
    sync_release(fe->file_lock);

    dprintf(FILE_CACHEING_VERBOSITY, "read all done %d\n", proc->pid);
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
    // Invalidate dir_cache since we are modifying this file
    dprintf(FILE_CACHEING_VERBOSITY, "write dir lock before\n");
    sync_acquire(dir_cache_lock);
    int hash = hash_string(fe->name);
    dir_cache[hash].path[0] = '\0';
    sync_release(dir_cache_lock);

    int nwritten = 0;

    dprintf(FILE_CACHEING_VERBOSITY, "write file lock before %d\n", proc->pid);
    sync_acquire(fe->file_lock);
    dprintf(FILE_CACHEING_VERBOSITY, "write file lock after %d\n", proc->pid);

    uint32_t aligned_offset = PAGE_ALIGN(offset, PAGE_SIZE);
    offset -= aligned_offset;

    dprintf(FILE_CACHEING_VERBOSITY, "write cache lock before %d\n", proc->pid);
    sync_acquire(cache_lock);
    dprintf(FILE_CACHEING_VERBOSITY, "write cache lock after %d\n", proc->pid);

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

            dprintf(FILE_CACHEING_VERBOSITY, "write copyin before %d\n", proc->pid);
            int err = copyin(proc, cur->data + offset, usr_buf, to_write);
            dprintf(FILE_CACHEING_VERBOSITY, "write copyin after %d\n", proc->pid);
            if (err) {
                sync_acquire(cache_lock);
                cur->pinned = false;
                sync_release(cache_lock);
                sync_release(fe->file_lock);
                return -err;
            }

            nwritten += to_write;
            nbytes -= to_write;
            usr_buf += to_write;
            offset = 0;
            aligned_offset += PAGE_SIZE;

            dprintf(FILE_CACHEING_VERBOSITY, "write re-acquire %d\n", proc->pid);
            sync_acquire(cache_lock);
            dprintf(FILE_CACHEING_VERBOSITY, "write re-acquire done %d\n", proc->pid);
            
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
            dprintf(FILE_CACHEING_VERBOSITY, "write nfs_write before %d\n", proc->pid);
            int nfs_nwritten = nfs_write_sync(proc, &fe->fh, aligned_offset + offset, usr_buf, to_write);
            dprintf(FILE_CACHEING_VERBOSITY, "write nfs_write after %d\n", proc->pid);
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

            dprintf(FILE_CACHEING_VERBOSITY, "write else re-acquire %d\n", proc->pid);
            sync_acquire(cache_lock);
            dprintf(FILE_CACHEING_VERBOSITY, "write else re-acquire done %d\n", proc->pid);
        }
    }

    if (cur) cur->pinned = false;

    sync_release(cache_lock);

    sync_release(fe->file_lock);
    dprintf(FILE_CACHEING_VERBOSITY, "write all done %d\n", proc->pid);
    return nwritten;
}

void vfs_cache_clear_file(struct file_t *fe) {
    sync_acquire(fe->file_lock);
    sync_acquire(cache_lock);

    struct vfs_cache_entry *next_entry = NULL;
    for (struct vfs_cache_entry *cur = fe->cache_entry_head; cur != NULL; ) {
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
