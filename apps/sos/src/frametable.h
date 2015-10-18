#ifndef _FRAMETABLE_H_
#define _FRAMETABLE_H_

#include <sync/mutex.h>
#include "proc.h"

struct ft_entry {
    /* paddr should be 0 when the frame isn't allocated */
    seL4_Word paddr;
    seL4_CPtr cap;
    seL4_CPtr user_cap;

    process_t *vaddr_proc;
    seL4_Word vaddr;

    bool referenced;

    uint32_t next_free;

    bool is_freeable;
    bool is_swappable;
} *ft;

sync_mutex_t ft_lock;

void frametable_init();

/* 
 * Allocates a physical frame to a vaddr, and returns
 * the vaddr. Sets frame bits according to arguments.
 */
seL4_Word frame_alloc(bool freeable, bool swappable);

/* 
 * Allocates an unswappable physical frame. Swaps a page
 * out if not enough space for it. This frame is meant 
 * to be used within sos.
 */
seL4_Word frame_alloc_sos(bool freeable);

/* 
 * Given a vaddr within SOS's address space, free
 * the frame associated with it.
 */
int frame_free(seL4_Word vaddr);

/* Given an sos addr, returns a frametable index */
uint32_t svaddr_to_frame_idx(seL4_Word vaddr);

/* Given a frametable index, returns an SOS addr */
seL4_Word frame_idx_to_svaddr(uint32_t idx);

/* Given a vaddr, get the frame's swappable bit */
int frame_get_swappable(seL4_Word svaddr);

/* Given a vaddr, set the frame's swappable bit */
int frame_change_swappable(seL4_Word svaddr, bool swappable);

/* Given a vaddr, and permissions, change the frame's permissions */
int frame_change_permissions(seL4_Word svaddr, seL4_CapRights rights, seL4_ARM_VMAttributes attr);

/* Returns number of frames held in frame table */
void get_ft_limits(size_t *ret_lo, size_t *ret_hi);

#endif /* _FRAMETABLE_H_ */
