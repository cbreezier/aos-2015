#ifndef _FRAMETABLE_H_
#define _FRAMETABLE_H_

#include <sync/mutex.h>
#include "proc.h"

struct ft_entry {
    /* paddr should be 0 when the frame isn't allocated */
    /* paddr obtained from ut_alloc */
    seL4_Word paddr;

    /*
     * SOS cap for the frame
     * This always exists as the page is always mapped in for SOS
     * as long as the frame is allocated
     */
    seL4_CPtr cap;
    /*
     * Copied cap to map in a page for the user
     * This is deleted and set to 0 when a user page is temporarily unmapped
     * (due to clock algorithm)
     */
    seL4_CPtr user_cap;

    /* User process and vaddr associated with this page */
    process_t *vaddr_proc;
    seL4_Word vaddr;

    /* Used for second chance replacement clock algorithm */
    bool referenced;

    uint32_t next_free;

    bool is_freeable;
    bool is_swappable;
} *ft;

/* 
 * Frame table lock. This is pretty much acquired for the entire
 * frame table upon any operation involving frames or pages
 */
sync_mutex_t ft_lock;

/*
 * Initialises the frametable, filling in the first few frames with
 * itself. Initialises ft_lock.
 */
void frametable_init();

/* 
 * Allocates a physical frame to a svaddr, and returns
 * the svaddr. Sets frame bits according to arguments.
 *
 * Returns the base svaddr of the allocated frame
 */
seL4_Word frame_alloc(bool freeable, bool swappable);

/* 
 * Allocates an unswappable physical frame. Swaps a page
 * out if not enough space for it. This frame is meant 
 * to be used within sos.
 *
 * Returns the base svaddr of the allocated frame
 */
seL4_Word frame_alloc_sos(bool freeable);

/* 
 * Given a vaddr within SOS's address space, free
 * the frame associated with it.
 *
 * Returns 0 on success
 */
int frame_free(seL4_Word vaddr);

/* Given an sos addr, returns a frametable index */
uint32_t svaddr_to_frame_idx(seL4_Word vaddr);

/* Given a frametable index, returns an SOS svaddr */
seL4_Word frame_idx_to_svaddr(uint32_t idx);

/* Given an svaddr, set the frame's swappable bit */
int frame_change_swappable(seL4_Word svaddr, bool swappable);

/* Given an svaddr, and permissions, change the frame's permissions */
int frame_change_permissions(seL4_Word svaddr, seL4_CapRights rights, seL4_ARM_VMAttributes attr);

/* Returns number of frames held in frame table */
void get_ft_limits(size_t *ret_lo, size_t *ret_hi);

#endif /* _FRAMETABLE_H_ */
