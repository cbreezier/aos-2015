#include <sync/mutex.h>
#include <stdlib.h>
#include <assert.h>
#include <bits/limits.h>
#include <sync/alloc_wrappers.h>


#define MUTEX_MAGIC 0x5EED

sync_mutex_t
sync_create_mutex() {
    sync_mutex_t mutex;

    mutex = (sync_mutex_t) kmalloc(sizeof(struct sync_mutex_));
    if (!mutex)
        return NULL;

    mutex->ep = sync_new_ep(&mutex->mapping, MUTEX_MAGIC);
    if(mutex->ep == NULL){
        kfree(mutex);
        return NULL;
    }

    // Prime the endpoint
    mutex->holder = 0;
    seL4_Notify(mutex->mapping, 0);
    return mutex;
}

void
sync_destroy_mutex(sync_mutex_t mutex) {
    sync_free_ep(mutex->ep);
    kfree(mutex);
}

static uint32_t get_thread_id() {
    return (uint32_t)seL4_GetIPCBuffer();
}

void
sync_acquire(sync_mutex_t mutex) {
    seL4_Word badge;
    assert(mutex);

    if (mutex->holder == get_thread_id()) {
        mutex->hold_count++;
        return;
    }

    seL4_Wait(mutex->mapping, &badge);
    assert(badge == MUTEX_MAGIC);

    mutex->holder = get_thread_id();
    mutex->hold_count = 1;
}

void
sync_release(sync_mutex_t mutex) {
    assert(mutex);
    if (mutex->holder != get_thread_id()) {
        return;
    }  

    mutex->hold_count--;
    if (mutex->hold_count == 0) {
        mutex->holder = 0;

        // Wake the next guy up
        seL4_Notify(mutex->mapping, 0);
    }
}

int
sync_try_acquire(sync_mutex_t mutex) {
    seL4_Word badge;
    seL4_Poll(mutex->mapping, &badge);
    return badge == MUTEX_MAGIC;
}


