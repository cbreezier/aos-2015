#include <addrspace.h>
#include <vmem_layout.h>
#include <string.h>
#include <stdlib.h>
#include <pagetable.h>
#include <sys/panic.h>
#include <ut_manager/ut.h>

#define MEMORY_TOP (0xFFFFFFFF)
#define STACK_SIZE (0x40000000)
#define HEAP_SIZE (0x10000000)

int as_init(struct addrspace **as) {
    *as = NULL;
    struct addrspace *new = malloc(sizeof(struct addrspace));
    if (new == NULL) {
        return ENOMEM;
    }

    new->region_head = NULL;
    new->page_directory = malloc(sizeof(struct pt_entry*) * (1 << TOP_LEVEL_SIZE));
    if (new->page_directory == NULL) {
        return ENOMEM;
    }
    for (int i = 0; i < (1 << TOP_LEVEL_SIZE); ++i) {
        new->page_directory[i] = NULL;
    }

    // Values of caps are default initialised - don't assume anything about their value
    new->pt_caps = malloc(sizeof(seL4_CPtr) * (1 << TOP_LEVEL_SIZE));
    if (new->pt_caps == NULL) {
        return ENOMEM;
    }
    memset(new->pt_caps, 0, sizeof(seL4_CPtr) * (1 << TOP_LEVEL_SIZE));

    new->pt_addrs = malloc(sizeof(seL4_Word) * (1 << TOP_LEVEL_SIZE));
    if (new->pt_addrs== NULL) {
        return ENOMEM;
    }
    memset(new->pt_addrs, 0, sizeof(seL4_Word) * (1 << TOP_LEVEL_SIZE));


    *as = new;
    return 0;
}

int as_destroy(struct addrspace *as) {
    if (as == NULL) return 0;
    /* Free region list */
    struct region_entry *cur;
    struct region_entry *prev = NULL;
    for (cur = as->region_head; cur != NULL; cur = cur->next) {
        if (prev != NULL) {
            free(prev);
        }
        prev = cur;
    }

    /*
     * Free all the frames allocated in the page table
     * as well as the page table itself
     */
    if (as->page_directory != NULL) {
        for (size_t l1 = 0; l1 < (1 << TOP_LEVEL_SIZE); ++l1) {
            if (as->page_directory[l1] == NULL) {
                continue;
            }
            for (size_t l2 = 0; l2 < (1 << SECOND_LEVEL_SIZE); ++l2) {
                if (as->page_directory[l1][l2].frame == 0) {
                    continue;
                }
                pt_remove_page(&as->page_directory[l1][l2]);
            }
            free(as->page_directory[l1]);
        }
        free(as->page_directory);
    }

    /* Free the kernel PageTable where relevant */
    for (size_t l1 = 0; l1 < (1 << TOP_LEVEL_SIZE); ++l1) {
        if (as->pt_caps[l1]) {
            int err = cspace_revoke_cap(cur_cspace, as->pt_caps[l1]);
            conditional_panic(err, "Unable to revoke cap(as_destroy)");

            err = cspace_delete_cap(cur_cspace, as->pt_caps[l1]);
            conditional_panic(err, "Unable to delete cap(as_destroy)");

            ut_free(as->pt_addrs[l1], seL4_PageTableBits);
        }
    }
    return 0;
}

static int as_do_add_region(struct addrspace *as, seL4_Word start, size_t size, bool r, bool w, bool x, struct region_entry **ret) {
    bool invalid_location = (size == 0 || size == MEMORY_TOP || start < 0 || start > MEMORY_TOP - size - 1);
    if (as == NULL || invalid_location) {
        return EINVAL;
    }
    printf("Adding region at 0x%08x, size 0x%08x\n", start, size);

    // Round down starting vaddr
    start = (start / PAGE_SIZE) * PAGE_SIZE;
    // Round up size
    size = ((size - 1) / PAGE_SIZE + 1) * PAGE_SIZE;

    struct region_entry *prev = NULL;
    struct region_entry *cur = as->region_head;

    for (; cur != NULL; cur = cur->next) {
        if (start >= cur->start && start < cur->start + cur->size) {
            return EINVAL;
        }
        if (cur->start >= start && cur->start < start + size) {
            return EINVAL;
        }

        if (start < cur->start) {
            break;
        }
        prev = cur;
    }

    // Found where to insert region to maintain sorted order
    struct region_entry *new_region = malloc(sizeof(struct region_entry));
    if (new_region == NULL) {
        return ENOMEM;
    }
    new_region->start = start;
    new_region->size = size;
    new_region->r = r;
    new_region->w = w;
    new_region->x = x;
    new_region->next = cur;

    if (prev == NULL) {
        as->region_head = new_region;
    } else {
        prev->next = new_region;
    }

    *ret = new_region;

    return 0;
}

int as_add_region(struct addrspace *as, seL4_Word start, size_t size, bool r, bool w, bool x) {
    struct region_entry *unused;
    return as_do_add_region(as, start, size, r, w, x, &unused);
}

int as_add_stack(struct addrspace *as) {
    return as_do_add_region(as, PROCESS_STACK_TOP - STACK_SIZE + 1, STACK_SIZE , 1, 1, 0, &(as->stack_region));
}

int as_add_heap(struct addrspace *as) {
    seL4_Word start = 0;
    struct region_entry *cur;
    /* Search for the end position of the last region */
    for (cur = as->region_head; cur != NULL; cur = cur->next) {
        start = cur->start+cur->size;
    }
    return as_do_add_region(as, start, HEAP_SIZE, 1, 1, 0, &(as->heap_region));
}
