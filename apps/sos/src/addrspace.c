#include <string.h>
#include <stdlib.h>
#include <sys/panic.h>
#include <sys/debug.h>
#include <ut_manager/ut.h>
#include "vmem_layout.h"
#include "addrspace.h"
#include "pagetable.h"
#include "frametable.h"
#include "alloc_wrappers.h"

#define MEMORY_TOP (0xFFFFFFFF)
#define STACK_SIZE (0x40000000)

int as_init(struct addrspace **ret_as) {
    assert(ret_as != NULL);

    *ret_as = NULL;
    dprintf(0, "allocing addrspace\n");

    seL4_DebugPutChar('A');
    struct addrspace *new = kmalloc(sizeof(struct addrspace));
    seL4_DebugPutChar('B');
    fflush(0);
    dprintf(0, "done allocing addrspace\n");
    if (new == NULL) {
        return ENOMEM;
    }

    new->region_head = NULL;
    assert(PAGE_SIZE == sizeof(struct pt_entry*)*(1 << TOP_LEVEL_SIZE));
    new->page_directory = (struct pt_entry**)frame_alloc_sos(true);
    if (new->page_directory == NULL) {
        return ENOMEM;
    }
    memset(new->page_directory, 0, PAGE_SIZE);

    /* 
     * Allocating only 1 frame under the assumption that the top_level_size is 10
     * ie there's exactly one frame necessary to store the pagetable caps and addrs
     */
    assert(PAGE_SIZE == sizeof(seL4_CPtr)*(1 << TOP_LEVEL_SIZE));
    assert(PAGE_SIZE == sizeof(seL4_Word)*(1 << TOP_LEVEL_SIZE));
    new->pt_caps = (seL4_CPtr*)frame_alloc_sos(true);
    if (new->pt_caps == 0) {
        return ENOMEM;
    }
    memset(new->pt_caps, 0, PAGE_SIZE);

    new->pt_addrs = (seL4_Word *)frame_alloc_sos(true);
    if (new->pt_addrs == 0) {
        return ENOMEM;
    }
    memset(new->pt_addrs, 0, PAGE_SIZE);

    *ret_as = new;
    return 0;
}

int as_destroy(process_t *proc) {
    struct addrspace *as = proc->as;
    if (as == NULL) return 0;
    /* Free region list */
    struct region_entry *cur;
    struct region_entry *prev = NULL;
    for (cur = as->region_head; cur != NULL; cur = cur->next) {
        if (prev != NULL) {
            kfree(prev);
        }
        prev = cur;
    }
    if (prev != NULL) {
        kfree(prev);
    }

    int err = 0;
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
                pt_remove_page(proc, &as->page_directory[l1][l2]);
                as->page_directory[l1][l2].frame = 0;
            }
            err = frame_free((seL4_Word)as->page_directory[l1]);
            conditional_panic(err, "Unable to delete page table");
        }
        err = frame_free((seL4_Word)as->page_directory);
        conditional_panic(err, "Unable to delete page directory");
    }

    /* Free the kernel PageTable where relevant */
    for (size_t l1 = 0; l1 < (1 << TOP_LEVEL_SIZE); ++l1) {
        if (as->pt_caps[l1]) {
            err = cspace_revoke_cap(cur_cspace, as->pt_caps[l1]);
            conditional_panic(err, "Unable to revoke cap(as_destroy)");

            err = cspace_delete_cap(cur_cspace, as->pt_caps[l1]);
            conditional_panic(err, "Unable to delete cap(as_destroy)");

            kut_free(as->pt_addrs[l1], seL4_PageTableBits);
        }
    }

    err = frame_free((seL4_Word)as->pt_caps);
    conditional_panic(err, "Cannot free pt_caps frame");
    
    err = frame_free((seL4_Word)as->pt_addrs);
    conditional_panic(err, "Cannot free pt_addrs frame");
    
    kfree(as);
    return 0;
}

static int as_do_add_region(struct addrspace *as, seL4_Word start, size_t size, bool r, bool w, bool x, struct region_entry **ret) {

    // Round down starting vaddr
    start = (start / PAGE_SIZE) * PAGE_SIZE;
    // Round up size
    size = ((size - 1) / PAGE_SIZE + 1) * PAGE_SIZE;

    bool invalid_location = (size == 0 || size == MEMORY_TOP || start < 0 || start > MEMORY_TOP - size - 1);
    if (as == NULL || invalid_location) {
        return EINVAL;
    }

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
    struct region_entry *new_region = kmalloc(sizeof(struct region_entry));
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

int as_add_stack(process_t *proc) {
    int err = as_do_add_region(proc->as, PROCESS_STACK_TOP - STACK_SIZE, STACK_SIZE , 1, 1, 0, &(proc->as->stack_region));
    if (err) {
        return err;
    }
    err = pt_add_page(proc, PROCESS_STACK_TOP - STACK_SIZE, NULL, NULL);
    return err;
}

int as_add_heap(struct addrspace *as) {
    seL4_Word start = PROCESS_HEAP_START;
    return as_do_add_region(as, start, PROCESS_HEAP_SIZE, 1, 1, 0, &(as->heap_region));
}

int as_search_add_region(struct addrspace *as, seL4_Word min, size_t size, bool r, bool w, bool x, seL4_Word *ret_insert_location) {
    assert(ret_insert_location != NULL);

    // Round down starting vaddr
    min = (min / PAGE_SIZE) * PAGE_SIZE;
    // Round up size
    size = ((size - 1) / PAGE_SIZE + 1) * PAGE_SIZE;

    bool invalid_location = (size == 0 || size == MEMORY_TOP || min <= 0 || min > MEMORY_TOP - size - 1);
    if (as == NULL || invalid_location) {
        return EINVAL;
    }

    struct region_entry *prev = NULL;
    struct region_entry *cur = as->region_head;

    /* Cannot insert at 0 so start at the next page */
    *ret_insert_location = min;

    bool found = false;
    for (; cur != NULL; cur = cur->next) {
        if (cur->start - *ret_insert_location > size) {
            found = true;
            break;
        }
        prev = cur;
        *ret_insert_location = cur->start + cur->size;
    }
    if (!found) {
        /* Case where we can add after the last region */
        if (prev != NULL && prev->start + prev->size + size <= MEMORY_TOP) {
            *ret_insert_location = prev->start + prev->size;
        } else {
            return EFAULT;
        }
    }

    // Found where to insert region to maintain sorted order
    struct region_entry *new_region = kmalloc(sizeof(struct region_entry));
    if (new_region == NULL) {
        return ENOMEM;
    }
    new_region->start = *ret_insert_location;
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

    return 0;

}

int as_remove_region(process_t *proc, seL4_Word addr) {
    struct addrspace *as = proc->as;

    if (as == NULL) return 0;
    struct region_entry *prev = NULL;
    struct region_entry *cur;

    addr = (addr / PAGE_SIZE) * PAGE_SIZE;
    size_t size = 0;
    /* Remove from region list */
    for (cur = as->region_head; cur != NULL; cur = cur->next) {
        if (cur->start == addr) {
            if (prev == NULL) {
                as->region_head = cur->next;
            } else {
                prev->next = cur->next;
            }
            size = cur->size;
            kfree(cur);
            break;
        }
    }

    /* Unmap memory associated with the region */
    if (size != 0 && as->page_directory != NULL) {
        /* Top level index begin */
        seL4_Word tl_idx_b = addr >> (SECOND_LEVEL_SIZE + OFFSET_SIZE);
        /* Second level index begin */
        seL4_Word sl_idx_b = (addr << TOP_LEVEL_SIZE) >> (TOP_LEVEL_SIZE + OFFSET_SIZE);

        addr += size - 1;

        /* Top level index end */
        seL4_Word tl_idx_e = addr >> (SECOND_LEVEL_SIZE + OFFSET_SIZE);
        /* Second level index end */
        seL4_Word sl_idx_e = (addr << TOP_LEVEL_SIZE) >> (TOP_LEVEL_SIZE + OFFSET_SIZE);

        for (seL4_Word tl_idx = tl_idx_b; tl_idx <= tl_idx_e; ++tl_idx) {
            if (as->page_directory[tl_idx] == NULL) {
                continue;
            }
            seL4_Word sl_idx = (tl_idx == tl_idx_b) ? sl_idx_b : 0;
            seL4_Word sl_idx_to = (tl_idx == tl_idx_e) ? sl_idx_e : ((1 << SECOND_LEVEL_SIZE) - 1);
            for (; sl_idx <= sl_idx_to; ++sl_idx) {
                if (as->page_directory[tl_idx][sl_idx].frame == 0) {
                    continue;
                }
                pt_remove_page(proc, &as->page_directory[tl_idx][sl_idx]);
                as->page_directory[tl_idx][sl_idx].frame = 0;
            }
        }
    }

    return 0;
}

struct region_entry *as_get_region(struct addrspace *as, void *vaddr) {
    struct region_entry *region;
    for (region = as->region_head; region != NULL; region = region->next) {
        if ((seL4_Word)vaddr >= region->start && (seL4_Word)vaddr < region->start + region->size) {
            return region;
        }
    }
    return NULL;
}
