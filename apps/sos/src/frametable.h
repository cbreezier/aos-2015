#ifndef _FRAMETABLE_H_
#define _FRAMETABLE_H_

#include <sos.h>

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
#endif /* _FRAMETABLE_H_ */
