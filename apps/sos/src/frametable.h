#ifndef _FRAMETABLE_H_
#define _FRAMETABLE_H_

#include <sos.h>
#include <sync/mutex.h>

struct ft_entry {
    seL4_Word paddr;
    seL4_CPtr cap;
    seL4_CPtr user_cap;

    uint32_t next_free;

    uint32_t is_freeable : 1;
    uint32_t is_swappable : 1;
} *ft;

sync_mutex_t ft_lock;

void frametable_init();

/* 
 * Allocates a physical frame to a vaddr, and returns
 * the vaddr. Sets frame bits according to arguments.
 */
seL4_Word frame_alloc(bool freeable, bool swappable);

/* 
 * Given a vaddr within SOS's address space, free
 * the frame associated with it.
 */
int frame_free(seL4_Word vaddr);

/* Given an sos addr, returns a frametable index */
uint32_t vaddr_to_frame_idx(seL4_Word vaddr);

/* Given a frametable index, returns an SOS addr */
seL4_Word frame_idx_to_vaddr(uint32_t idx);

#endif /* _FRAMETABLE_H_ */
