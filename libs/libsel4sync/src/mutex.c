#include <sync/mutex.h>
#include <stdlib.h>
#include <assert.h>
#include <bits/limits.h>


#define MUTEX_MAGIC 0x5EED

struct sync_mutex_ {
    void* ep;
    seL4_CPtr mapping;

    uint32_t holder;
};

sync_mutex_t
sync_create_mutex() {
    sync_mutex_t mutex;

    mutex = (sync_mutex_t) malloc(sizeof(struct sync_mutex_));
    if (!mutex)
        return NULL;

    printf("sync new ep\n");
    mutex->ep = sync_new_ep(&mutex->mapping, MUTEX_MAGIC);
    printf("done\n");
    if(mutex->ep == NULL){
        free(mutex);
        return NULL;
    }

    printf("sync releasing\n");
    // Prime the endpoint
    sync_release(mutex);
    printf("done2\n");
    return mutex;
}

void
sync_destroy_mutex(sync_mutex_t mutex) {
    sync_free_ep(mutex->ep);
    free(mutex);
}

static uint32_t get_thread_id() {
    seL4_Word unused;
    return (int)(&unused) / PAGE_SIZE;
}

void
sync_acquire(sync_mutex_t mutex) {
    seL4_Word badge;
    assert(mutex);

    if (mutex->holder == get_thread_id()) {
        return;
    }

    seL4_Wait(mutex->mapping, &badge);
    assert(badge == MUTEX_MAGIC);
    mutex->holder = get_thread_id();
}

void
sync_release(sync_mutex_t mutex) {
    assert(mutex);
    mutex->holder = 0;

    // Wake the next guy up
    seL4_Notify(mutex->mapping, 0);
}

int
sync_try_acquire(sync_mutex_t mutex) {
    seL4_Word badge;
    seL4_Poll(mutex->mapping, &badge);
    return badge == MUTEX_MAGIC;
}


