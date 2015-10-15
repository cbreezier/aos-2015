#ifndef _KMALLOC_H_
#define _KMALLOC_H_

#include <sel4/sel4.h>
#include <stdlib.h>
#include <sync/mutex.h>

sync_mutex_t printf_lock;
sync_mutex_t malloc_lock;

/* Initialise the locks */
void alloc_wrappers_init();

/* Threadsafe malloc */
void *kmalloc(size_t n);

/* Threadsafe free */
void kfree(void *buf);

/* Threadsafe ut_alloc */
seL4_Word kut_alloc(int sizebits);

/* Threadsafe ut_free */
void kut_free(seL4_Word addr, int sizebits);

#endif /* _KMALLOC_H_ */
