#ifndef _FRAMETABLE_H_
#define _FRAMETABLE_H_

#include <sos.h>

void frametable_init();

uint32_t frame_alloc(seL4_Word *vaddr);

int frame_free(uint32_t idx);
#endif /* _FRAMETABLE_H_ */
