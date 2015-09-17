#include <sync/mutex.h>
#include <sys/panic.h>
#include <stdlib.h>
#include "kmalloc.h"

sync_mutex_t malloc_lock;

void kmalloc_init() {
    malloc_lock = sync_create_mutex();
    conditional_panic(!malloc_lock, "Cannot create malloc lock");
}

void *kmalloc(size_t n) {
    /* 
     * If the lock doesn't exist, don't acquire it. This is checked
     * in order to also use kmalloc when sos is initing.
     */
    if (malloc_lock) sync_acquire(malloc_lock);
    void *ret = malloc(n);
    if (malloc_lock) sync_release(malloc_lock);
    return ret;
}

void kfree(void *buf) {
    /* 
     * If the lock doesn't exist, don't acquire it. This is checked
     * in order to also use kfree when sos is initing.
     */
    if (malloc_lock) sync_acquire(malloc_lock);
    free(buf);
    if (malloc_lock) sync_release(malloc_lock);
}
