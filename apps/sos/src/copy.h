#ifndef _COPY_H_
#define _COPY_H_

#include <sel4/sel4.h>
#include "proc.h"
#include "addrspace.h"

/*
 * Checks if a segment of user memory starting at address usr_buf of
 * size buf_size fits entirely within a single valid region.
 *
 * ret_region_r and ret_region_w will be optionally filled with the
 * readable and writeable permissions of the identified region if not NULL
 *
 * Returns true if a region encompassing the memory exists, false otherwise
 */
bool usr_buf_in_region(process_t *proc, void *usr_buf, size_t buf_size, bool *ret_region_r, bool *ret_region_w);

/*
 * Given a segment of user memory starting at usr_buf of size buf_size,
 * map in the first relevant frame and make it accessible from SOS as an svaddr.
 * Usually used in a loop and called multiple times for manual copyin/out.
 *
 * ret_svaddr contains the svaddr which corresponds to usr_buf
 * ret_buf_page_left contains the number of bytes left in the current page
 *
 * Pins the mapped in page,
 * make sure to make the frame swappable after using it
 *
 * Returns 0 on success and an error code otherwise
 */
int usr_buf_to_sos(process_t *proc, void *usr_buf, size_t buf_size, seL4_Word *ret_svaddr, size_t *ret_buf_page_left);

/*
 * Copy in nbytes from user space into SOS space.
 *
 * dest is a SOS address
 * src is a user address
 *
 * Returns 0 on success and error code otherwise
 */
int copyin(process_t *proc, void *dest, void *src, size_t nbytes); 

/*
 * Same as copyin but terminates early on a terminating NULL.
 * May pad with extra NULLs.
 *
 * Returns 0 on success and error code otherwise
 */
int copyinstring(process_t *proc, void *dest, void *src, size_t nbytes);

/* See above */
int copyout(process_t *proc, void *dest, void *src, size_t nbytes); 

/* See above */
int copyoutstring(process_t *proc, void *dest, void *src, size_t nbytes); 

#endif /* _COPY_H_ */
