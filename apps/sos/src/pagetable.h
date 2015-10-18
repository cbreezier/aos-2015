#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include "addrspace.h"

/*
 * Given a process, and a virtual address, maps a page in that process's address space.
 * This may involve swapping the page in from disk, if located in the swap file,
 * or simply swapping a page out. Additionally, it is possible that the
 * page is already present in memory (from second-change page-replacement algorithm),
 * in which case pt_add_page simply maps the page in.
 *
 * Optionally returns the corresponding svaddr for the mapped user page.
 * If ret_svaddr is requested, pt_add_page also pins the frame.
 *
 * Optinally returns the cap associated with the frame.
 *
 * Returns 0 if successful, non-zero if error.
 */
int pt_add_page(process_t *proc, seL4_Word vaddr, seL4_Word *ret_svaddr, seL4_CPtr *ret_frame_cap);

/*
 * Removes a page from a user's address space.
 *
 * This cleans up all data associated with the page, including related SOS
 * frames, and swap file entries.
 */
void pt_remove_page(process_t *proc, struct pt_entry *pe);

/*
 * Given a user's virtual address, returns the pagetable entry associated
 * with that address.
 *
 * Returns NULL if the entry does not exist.
 */
struct pt_entry *vaddr_to_pt_entry(struct addrspace *as, seL4_Word vaddr);

#endif /* _PAGETABLE_H_ */
