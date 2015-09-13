#ifndef _SWAP_H_
#define _SWAP_H_

#include "proc.h"
#include <sel4/sel4.h>

#define SWAP_MULTIPLIER 10
#define SWAP_TABLE_SIZE (1 << 16) / PAGE_SIZE * SWAP_MULTIPLIER

struct swap_entry {
    int next_free;
} *swap_table;

void swap_init(size_t lo_ft_idx, size_t hi_ft_idx);

int swapin(process_t *proc, seL4_Word vaddr, seL4_Word *svaddr);

#endif /* _SWAP_H_ */
