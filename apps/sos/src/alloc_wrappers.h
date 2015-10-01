#ifndef _KMALLOC_H_
#define _KMALLOC_H_

#include <sel4/sel4.h>
#include <stdlib.h>
#include <sync/mutex.h>

sync_mutex_t printf_lock;

void alloc_wrappers_init();

void *kmalloc(size_t n);

void kfree(void *buf);

seL4_Word kut_alloc(int sizebits);

void kut_free(seL4_Word addr, int sizebits);

#endif /* _KMALLOC_H_ */
