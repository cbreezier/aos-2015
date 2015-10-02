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

/* 
 * If ret_svaddr is 0, swapin first swaps out another frame.
 * If ret_svaddr is not zero, swapin uses *ret_svaddr as the location
 * in the frametable at which to insert 
 */
int swapin(process_t *proc, seL4_Word vaddr, seL4_Word *ret_svaddr);

int swapin_sos(seL4_Word *ret_svaddr);

void free_swap_entry(int entry_idx);

#endif /* _SWAP_H_ */
