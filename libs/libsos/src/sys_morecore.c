/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <autoconf.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>
#include <vmem_layout.h>
#include <syscall.h>
#include <sel4/sel4.h>
#include <sel4/types.h>
#include <sel4/constants.h>

#define SYSCALL_ENDPOINT_SLOT (1)

/*
 * Statically allocated morecore area.
 *
 * This is rather terrible, but is the simplest option without a
 * huge amount of infrastructure.
 */
//#define MORECORE_AREA_BYTE_SIZE 0x100000
//char morecore_area[MORECORE_AREA_BYTE_SIZE];

/* Pointer to free space in the morecore area. */
//static uintptr_t morecore_base = (uintptr_t) &morecore_area;
//static uintptr_t morecore_top = (uintptr_t) &morecore_area[MORECORE_AREA_BYTE_SIZE];

/* Actual morecore implementation
   returns 0 if failure, returns newbrk if success.
*/

static uintptr_t heap_base = PROCESS_HEAP_START;
static uintptr_t heap_top = PROCESS_HEAP_START + PROCESS_HEAP_SIZE;

static uintptr_t heap_cur = PROCESS_HEAP_START;

long
sys_brk(va_list ap)
{
    uintptr_t ret;
    uintptr_t newbrk = va_arg(ap, uintptr_t);
    //printf("newbrk = %u, heap_base = %u, heap_top = %u, heap_cur = %u\n", newbrk, heap_base, heap_top, heap_cur);

    /*if the newbrk is 0, return the bottom of the heap*/
    if (!newbrk) {
        ret = heap_cur = heap_base;
    } else if (newbrk < heap_top && newbrk >= heap_base) {
        ret = heap_cur = newbrk;
    } else if (newbrk >= heap_top) {
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 2*4);
        seL4_SetTag(tag);
        seL4_SetMR(0, SYS_brk);

        seL4_SetMR(1, newbrk);

        seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

        seL4_Word err = seL4_GetMR(0);
        if (err) {
            ret = 0;
        } else {
            ret = heap_cur = heap_top = seL4_GetMR(1);
        }
   
    } else {
        ret = 0;
    }

    return ret;
}

/* Large mallocs will result in muslc calling mmap, so we do a minimal implementation
   here to support that. We make a bunch of assumptions in the process */
long
sys_mmap2(va_list ap)
{
    void *addr = va_arg(ap, void*);
    size_t length = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 7*4);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_mmap2);

    seL4_SetMR(1, (seL4_Word)addr);
    seL4_SetMR(2, (seL4_Word)length);
    seL4_SetMR(3, (seL4_Word)prot);
    seL4_SetMR(4, (seL4_Word)flags);
    seL4_SetMR(5, (seL4_Word)fd);
    seL4_SetMR(6, (seL4_Word)offset);

    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    long err = seL4_GetMR(0);
    if (err) {
        return -err;
    }
    return seL4_GetMR(1);
}

long sys_munmap(va_list ap) {
    void *addr = va_arg(ap, void*);
    size_t length = va_arg(ap, size_t);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 3*4);
    seL4_SetTag(tag);
    seL4_SetMR(0, SYS_munmap);

    seL4_SetMR(1, (seL4_Word)addr);
    seL4_SetMR(2, (seL4_Word)length);

    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    return seL4_GetMR(0);
}

long
sys_mremap(va_list ap)
{
    assert(!"not implemented");
    return -ENOMEM;
}
