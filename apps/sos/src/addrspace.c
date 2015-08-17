#include <addrspace.h>
#include <vmem_layout.h>

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
    new->page_caps = malloc(sizeof(seL4_CPtr) * (1 << TOP_LEVEL_SIZE));
    if (new->page_caps == NULL) {
        return ENOMEM;
    }

    *as = new;
    return 0;
}

static int as_do_add_region(struct addrspace *as, seL4_Word start, size_t size, bool r, bool w, bool x, struct region_entry **ret) {
    bool invalid_location = (size == 0 || size == MEMORY_TOP || start < 0 || start > MEMORY_TOP - size - 1);
	if (as == NULL || invalid_location) {
		return EINVAL;
	}

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
    // struct region_entry *cur;
    // /* Search for the end position of the last region */
    // for (cur = as->region_head; cur != NULL; cur = cur->next) {
    //     start = cur->start+cur->size;
    // }
    return as_do_add_region(as, start, HEAP_SIZE, 1, 1, 0, &(as->heap_region));
}
