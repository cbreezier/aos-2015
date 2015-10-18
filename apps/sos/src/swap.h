#ifndef _SWAP_H_
#define _SWAP_H_

#include "proc.h"
#include <sel4/sel4.h>

/* Defines the size of the swap file, relative to available memory */
#define SWAP_MULTIPLIER 10

/*
 * Linked list of free swap file entries, each of size PAGE_SIZE.
 */
struct swap_entry {
    int next_free;
} *swap_table;

/*
 * Initialises data related to swap file book-keeping, given bounds
 * of the frametable. 
 *
 * Note that some frames will be allocated for the free swap file entries
 * list.
 */
void swap_init(size_t lo_ft_idx, size_t hi_ft_idx);

/* 
 * Given a user virtual address, places the page into a location in memory.
 *
 * Preconditions: Pagetable entry frame number should be less than or equal to zero.
 * If equal to 0, swapin also zeros the frame.
 * 
 * ret_svaddr modifies the behavior of swapin:
 *
 *      If ret_svaddr is 0, swapin first swaps out another frame, and then
 *      sets the value pointed to by ret_svaddr to be the svaddr of the
 *      frame at which the page has been placed into.
 *
 *      If ret_svaddr is not zero, swapin uses *ret_svaddr as the location
 *      in the frametable at which to insert. Nothing gets swapped out,
 *      and as such, *ret_svaddr must be a free frame.
 *
 *      Note that in both cases, the frame is not pinned.
 *
 * Returns 0 if successful, non-zero if error.
 */
int swapin(process_t *proc, seL4_Word vaddr, seL4_Word *ret_svaddr);

/*
 * Swaps out a user page, and assigns the corresponding frame to belong to SOS.
 *
 * Note that the frame is zero'd out, and pinned.
 *
 * Optional returns for the svaddr of the assigned frame.
 *
 * Returns 0 if successful, non-zero if error.
 */
int swapin_sos(seL4_Word *ret_svaddr);

/*
 * Given an entry in the swap file, frees any association with a user page,
 * and places it back into the free swap file entry list.
 *
 * This should only be called if the pagetable entry frame number is negative.
 * The index is then the absolute value of the frame number.
 */
void free_swap_entry(int entry_idx);

#endif /* _SWAP_H_ */
