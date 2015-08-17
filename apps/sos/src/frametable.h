#ifndef _FRAMETABLE_H_
#define _FRAMETABLE_H_

#include <sos.h>

struct ft_entry {
    seL4_Word seL4_id;
    seL4_CPtr cap;

    uint32_t next_free;

    uint32_t is_freeable : 1;
    uint32_t is_swapable : 1;
} *ft;

void frametable_init();

/* 
 * Allocates a physical frame to a vaddr, and returns
 * an id which should be used to later free the frame
 */
uint32_t frame_alloc(seL4_Word *vaddr);

/* 
 * Given an id previously provided by frame_alloc,
 * frees the frame associated with that id.
 */
int frame_free(uint32_t idx);

/* Given a frametable index, returns an SOS addr */
seL4_Word frame_idx_to_addr(uint32_t idx);
#endif /* _FRAMETABLE_H_ */
