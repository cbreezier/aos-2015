#include <stdio.h>
#include <sys/debug.h>
#include <sys/panic.h>
#include <stdlib.h>
#include <stdarg.h>
#include "ut_manager/ut.h"
#include "alloc_wrappers.h"

sync_mutex_t ut_lock;

int count;

struct _entry {
    void *addr;
    size_t size;
    struct _entry *next;
} malloc_entries[1000];

struct _entry *m_head;

struct _entry *free_m_head;

void alloc_wrappers_init() {
    count = 0;
    m_head = NULL;
    free_m_head = &malloc_entries[0];
    for (int i = 0; i < 999; i++) {
        malloc_entries[i].next = &malloc_entries[i+1];
    }
    malloc_entries[999].next = NULL;

    malloc_lock = sync_create_mutex();
    conditional_panic(!malloc_lock, "Cannot create malloc lock");

    ut_lock = sync_create_mutex();
    conditional_panic(!ut_lock, "Cannot create ut lock");

    printf_lock = sync_create_mutex();
    conditional_panic(!printf_lock, "Cannot create printf lock");

}

void *kmalloc(size_t n) {

    int a;
    /* 
     * If the lock doesn't exist, don't acquire it. This is checked
     * in order to also use kmalloc when sos is initing.
     */
    void *ret = NULL;
    if (malloc_lock) {
        sync_acquire(malloc_lock);

        ret = malloc(n);
        printf("malloc %p %d\n", ret, ++count);

        struct _entry *new = free_m_head;
        assert(new);
        free_m_head = free_m_head->next;

        new->addr = ret;
        new->size = n;
        new->next = m_head;
        m_head = new;
    } else {
        dprintf(0, "malloc lock null\n");
        ret = malloc(n);
        printf("lockless malloc %p %d\n", ret, ++count);
    }
    
    if (malloc_lock) sync_release(malloc_lock);
    else dprintf(0, "2nd malloc lock null\n");

    // // Print all mallocs so far
    // for (struct _entry *e = m_head; e != NULL; e = e->next) {
    //     printf(" > %p %u\n", e->addr, e->size);
    // }

    if (ret == 0x115950) {
        for (int i = 0; i < 12; i++) {
            printf("a + %d is %p, value %x\n", i, (&a) + i, *((&a) + i));
        }
    }
    return ret;
}

void kfree(void *buf) {
    /* 
     * If the lock doesn't exist, don't acquire it. This is checked
     * in order to also use kfree when sos is initing.
     */
    int a;
    if (malloc_lock) {
        sync_acquire(malloc_lock);
        struct _entry *prev = NULL;
        struct _entry *found = NULL;
        for (struct _entry *e = m_head; e != NULL; e = e->next) {
            if (e->addr == buf) {
                found = e;
                break;
            }
            prev = e;
        }
        if (!found) {
            printf("WARNING WARNING double free at %p\n", buf);
            for (int i = 0; i < 32; i++) {
                printf("a + %d is %p, value %x\n", i, (&a) + i, *((&a) + i));
            }
            assert(false);
            while (true);
        }
        free(buf);
        printf("free %p %d\n", buf, --count);

        if (prev) {
            prev->next = found->next;
        } else {
            m_head = found->next;
        }

        found->next = free_m_head;
        free_m_head = found;
    } else {
        free(buf);
        printf("lockless free %p %d\n", buf, --count);
    }

    if (malloc_lock) sync_release(malloc_lock);

    // // Print all mallocs so far
    // for (struct _entry *e = m_head; e != NULL; e = e->next) {
    //     printf(" < %p %u\n", e->addr, e->size);
    // }

    if (buf == 0x115950) {
        for (int i = 0; i < 12; i++) {
            printf("a + %d is %p, value %x\n", i, (&a) + i, *((&a) + i));
        }
    }
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
