#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

#include <sos.h>
#include <stdlib.h>
#include <sel4/types.h>
#include <bits/errno.h>
#include <bits/limits.h>


#define TOP_LEVEL_SIZE 10
#define SECOND_LEVEL_SIZE 10
#define OFFSET_SIZE 12

struct region_entry {
    seL4_Word start;
    size_t size;
    bool r;
    bool w;
    bool x;
    struct region_entry *next;
};

struct pt_entry {
    seL4_Word frame;
};

struct addrspace {
    struct region_entry *region_head;
    struct region_entry *stack_region;
    struct region_entry *heap_region;

    struct pt_entry **page_directory;
    seL4_CPtr *pt_caps;
    seL4_Word *pt_addrs;
};

int as_init(struct addrspace **as);

int as_destroy(struct addrspace *as);

int as_add_region(struct addrspace *as, seL4_Word start, size_t size, bool r, bool w, bool x);

int as_add_stack(struct addrspace *as);

int as_add_heap(struct addrspace *as);

#endif /* _ADDRSPACE_H_ */
