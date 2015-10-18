#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

#include <stdlib.h>
#include <sel4/types.h>
#include <bits/errno.h>
#include <bits/limits.h>
#include <stdint.h>
#include <stdbool.h>
#include "proc.h"


#define TOP_LEVEL_SIZE 10
#define SECOND_LEVEL_SIZE 10
#define OFFSET_SIZE 12

/* Linked list representing memory regions within an address space */
struct region_entry {
    /* Starting address and size of the region */
    seL4_Word start;
    size_t size;

    /* Read write execute permissions on the region */
    bool r;
    bool w;
    bool x;

    /* Next region in linked list */
    struct region_entry *next;
};

/* Page table entry
 * frame > 0
 *   Page is mapped in, contains frame number it is
 *   associated with
 * frame < 0
 *   Page is swapped out, contains swap file index
 *   that the memory is swapped out to
 * frame == 0
 *   Page is unmapped - this is the initial value
 */
struct pt_entry {
    int frame;
};

struct addrspace {
    /*
     * Regions are non-overlapping and are kept in sorted
     * order based on starting address
     */
    struct region_entry *region_head;
    struct region_entry *stack_region;
    struct region_entry *heap_region;

    /* Two level page table for bookkeeping */
    struct pt_entry **page_directory;
    /* Stores all the caps for the in-kernel second level tables */
    seL4_CPtr *pt_caps;
    /* Stores all the ut_alloc addresses for those caps */
    seL4_Word *pt_addrs;
};

/*
 * Initialise address space, creating the two level page table where
 * the lower levels are allocated memory lazily.
 * 
 * Returns 0 on success and an error code otherwise.
 */
int as_init(struct addrspace **ret_as);

/*
 * Destroys address space, freeing all memory and capabilities
 * associated with it.
 * 
 * Returns 0 on success and an error code otherwise.
 */
int as_destroy(process_t *proc);

/*
 * Adds a region starting at address "start" of size "size"
 * with readable, writeable and executable permissions as given
 * 
 * Returns 0 on success and an error code otherwise.
 */
int as_add_region(struct addrspace *as, seL4_Word start, size_t size, bool r, bool w, bool x);

/*
 * Specifically adds the stack of size STACK_SIZE, ending at PROCESS_STACK_TOP
 * Additionally it adds a guard region at the bottom of the stack
 *
 * Optionally pin the first stack page 
 * 
 * Returns 0 on success and an error code otherwise.
 */
int as_add_stack(process_t *proc, bool pin_pages);

/*
 * Specifically adds the heap of size PROCESS_HEAP_SIZE, starting at PROCESS_HEAP_START
 *
 * Optionally pins all initial heap pages
 * 
 * Returns 0 on success and an error code otherwise.
 */
int as_add_heap(process_t *proc, bool pin_pages);

/*
 * Similar to as_add_region but searches for the first place that a region
 * of size "size" could fit, beginning the search from address "min".
 *
 * ret_insert_location will be populated with the starting address of the region
 * that was chosen.
 *
 * Returns 0 on success and an error code otherwise.
 */
int as_search_add_region(struct addrspace *as, seL4_Word min, size_t size, bool r, bool w, bool x, seL4_Word *ret_insert_location);

/*
 * Removes the region which starts at address "addr" and unmaps and
 * frees any memory associated with that region
 *
 * Returns 0
 */
int as_remove_region(process_t *proc, seL4_Word addr);

/* Returns the region that a virtual address lies within.
 * NULL if not within a valid region
 */
struct region_entry *as_get_region(struct addrspace *as, void *vaddr);

/*
 * Changes the permissions of the region which contains the given "vaddr"
 *
 * Returns 0 on success and an error code otherwise
 */
int as_change_region_perms(struct addrspace *as, void *vaddr, bool r, bool w, bool x);

/*
 * Flushes the level 1 cache entry for every page that is mapped in
 * within the address space
 */
void as_unify_cache(struct addrspace *as);

#endif /* _ADDRSPACE_H_ */
