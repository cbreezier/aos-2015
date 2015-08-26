#include "sos_syscall.h"
#include <sys/mman.h>
#include <vmem_layout.h>
#include <clock/clock.h>

void sos_mmap2(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    void *addr = (void*)seL4_GetMR(1); 
    size_t length = (size_t)seL4_GetMR(2); 
    int prot = (int)seL4_GetMR(3); 
    int flags = (int)seL4_GetMR(4); 
    int fd = (int)seL4_GetMR(5); 
    off_t offset = (off_t)seL4_GetMR(6); 
   
    (void) addr; 
    (void) flags;
    (void) fd;
    (void) offset;
    (void) num_args;

    seL4_Word insert_location;

    int err = as_search_add_region(proc->as, PROCESS_VMEM_START, length, prot & PROT_WRITE, prot & PROT_READ, prot & PROT_EXEC, &insert_location);

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 8);
    seL4_SetMR(0, err);
    seL4_SetMR(1, insert_location);

    seL4_Send(reply_cap, reply);
}

void sos_munmap(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    void *addr = (void*)seL4_GetMR(1); 
    size_t length = (size_t)seL4_GetMR(2); 

    (void) length;
    (void) num_args;

    int err = as_remove_region(proc->as, (seL4_Word)addr);
    seL4_SetMR(0, err);

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 4);

    seL4_Send(reply_cap, reply);

}

static void reply_user(uint32_t id, void *data) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);

    seL4_SetMR(0, 0);

    seL4_Send(*((seL4_CPtr*)data), reply);

    free((seL4_CPtr*)data);
}   

void sos_nanosleep(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    (void) proc;
    (void) num_args;

    uint64_t delay = (uint64_t)seL4_GetMR(1);
    seL4_CPtr *data = malloc(sizeof(seL4_Word));
    int err = register_timer(delay, reply_user, (void*)data);
    if (err == 0) {
        seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 4);

        seL4_SetMR(0, EFAULT);

        seL4_Send(reply_cap, reply);
    }
}

void sos_clock_gettime(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    (void) proc;
    (void) num_args;

    uint64_t timestamp = time_stamp();

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2*4);

    seL4_SetMR(0, 0);
    seL4_SetMR(1, timestamp);

    seL4_Send(reply_cap, reply);
}
