#include <stdio.h>
#include <sys/debug.h>
#include <sys/panic.h>
#include <stdlib.h>
#include <stdarg.h>
#include "ut_manager/ut.h"
#include "alloc_wrappers.h"

sync_mutex_t ut_lock;

void alloc_wrappers_init() {
    malloc_lock = sync_create_mutex();
    conditional_panic(!malloc_lock, "Cannot create malloc lock");

    ut_lock = sync_create_mutex();
    conditional_panic(!ut_lock, "Cannot create ut lock");

    printf_lock = sync_create_mutex();
    conditional_panic(!printf_lock, "Cannot create printf lock");
}

void *kmalloc(size_t n) {
    /* 
     * If the lock doesn't exist, don't acquire it. This is checked
     * in order to also use kmalloc when sos is initing.
     */
    if (malloc_lock) sync_acquire(malloc_lock);
    else dprintf(0, "malloc lock null\n");
    void *ret = malloc(n);
    if (malloc_lock) sync_release(malloc_lock);
    else dprintf(0, "2nd malloc lock null\n");
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

seL4_Word kut_alloc(int sizebits) {
    if (ut_lock) sync_acquire(ut_lock);
    seL4_Word ret = ut_alloc(sizebits);
    if (ut_lock) sync_release(ut_lock);
    return ret;
}

void kut_free(seL4_Word addr, int sizebits) {
    if (ut_lock) sync_acquire(ut_lock);
    ut_free(addr, sizebits);
    if (ut_lock) sync_release(ut_lock);
}

//int kprintf(int verbosity, ...) {
//    int ret;
//    va_list ap;
//    va_start(ap, verbosity);
//    if (printf_lock) sync_acquire(printf_lock);    
//    ret = dprintf(verbosity, ap);
//    va_end(ap);
//    if (printf_lock) sync_release(printf_lock);    
//    return ret;
//}
