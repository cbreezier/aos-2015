#include "sos_syscall.h"
#include <sys/mman.h>
#include <vmem_layout.h>
#include <clock/clock.h>

void sos_mmap2(sos_process_t *proc, seL4_MessageInfo_t *reply) {
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

    seL4_Word insert_location;

    int err = as_search_add_region(proc->as, PROCESS_VMEM_START, length, prot & PROT_WRITE, prot & PROT_READ, prot & PROT_EXEC, &insert_location);

    *reply = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, err);
    seL4_SetMR(1, insert_location);
}

void sos_munmap(sos_process_t *proc, seL4_MessageInfo_t *reply) {
    void *addr = (void*)seL4_GetMR(1); 
    size_t length = (size_t)seL4_GetMR(2); 

    (void)length;

    int err = as_remove_region(proc->as, (seL4_Word)addr);
    seL4_SetMR(0, err);
}

void sos_time_stamp(sos_process_t *proc, seL4_MessageInfo_t *reply) {
    int64_t timestamp = time_stamp();

    int err = 0;
    if (timestamp < 0) {
        err = EFAULT;
    }

    seL4_SetMR(0, err);
    seL4_SetMR(1, timestamp);
}
