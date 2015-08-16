#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

#include <sos.h>
#include <stdlib.h>
#include <sel4/types.h>
#include <bits/errno.h>
#include <bits/limits.h>


#define TOP_LEVEL_SIZE 12
#define SECOND_LEVEL_SIZE 8
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
    uint32_t frame;
};

struct addrspace {
    struct region_entry *region_head;
    struct pt_entry **page_directory;
    seL4_CPtr *page_caps;
};

int as_init(struct addrspace **as);

int as_destroy(struct addrspace *as);

int as_add_region(struct addrspace *as, seL4_Word start, size_t size, bool r, bool w, bool x);

#endif /* _ADDRSPACE_H_ */
