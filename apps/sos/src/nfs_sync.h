#ifndef _NFS_SYNC_H_
#define _NFS_SYNC_H_

#include <nfs/nfs.h>
#include "file.h"
#include "proc.h"

/* 
 * NFS_SYNC contains a set of wrapper functions around the asynchronous 
 * nfs functions, such that they can be used as if synchronous.
 *
 * Note that deep sequences of lock acquisitions are used in most of
 * these wrappers, and as such, should never be used by ASYNC threads.
 */

/* 
 * Network lock should be acquired when handling network interrupts,
 * and accessing any un-synchronized libraries which send data
 * across a network, such as nfs.
 *
 * Note that no locks other than malloc_lock can be held at the same
 * time as it.
 */
sync_mutex_t network_lock;

void nfs_sync_init();

/*
 * Given a file name, returns a fhandle_t and fattr_t for that file, as given
 * by nfs.
 *
 * Returns 0 if successful, non-zero if error.
 */
int nfs_lookup_sync(const char *name, fhandle_t *ret_fh, fattr_t *ret_fattr);

/*
 * Reads nbytes at a specified offset from a file into a user's address space, 
 * starting at usr_buf.
 *
 * Returns the number of bytes read if successful, or a negative error code if
 * unsuccessful.
 */
int nfs_read_sync(process_t *proc, fhandle_t *fh, uint32_t offset, void *usr_buf, size_t nbytes);

/* 
 * Similar to nfs_read_sync. Differs in reading into a sos_buf, instead of usr_buf.
 *
 * Note that nbytes may be at most PAGE_SIZE.
 */
int nfs_sos_read_sync(fhandle_t fh, uint32_t offset, void *sos_buf, size_t nbytes);

/*
 * Writes nbytes into a specified offset within a file from a user's address space,
 * starting at usr_buf.
 *
 * Returns the number of bytes written if successful, or a negative error code if
 * unsuccessful.
 */
int nfs_write_sync(process_t *proc, fhandle_t *fh, uint32_t offset, void *usr_buf, size_t nbytes);

/* 
 * Similar to nfs_read_sync. Differs in writing from a sos_buf, instead of usr_buf.
 *
 * Note that nbytes may be at most PAGE_SIZE.
 */
int nfs_sos_write_sync(fhandle_t fh, uint32_t offset, void *sos_buf, size_t nbytes);

/*
 * Retrieves file names in the directory defined by mnt_point. 
 *
 * @param sos_buf | A buffer in which the file names will be held. There'll be at most
 * FILES_PER_DIR files, each with at most NAME_MAX characters. Therefore, to avoid
 * overflowing, sos_buf must be at least FILES_PER_DIR*NAME_MAX characters long.
 *
 * @param ret_num_files | Optional return for the number of files in the directory.
 *
 * Returns 0 if successful, non-zero if error.
 */
int nfs_readdir_sync(void *sos_buf, int *ret_num_files);


/*
 * Similar to nfs_readdir_sync, but only reads one file name, at a specified position.
 *
 * Returns -1 if the file position is invalid.
 *
 * Returns 0 if successful, greater than zero if error.
 *
 */
int nfs_readdir_pos_sync(void *sos_buf, int file_pos);

/*
 * Creates a file in the directory defined by mnt_point.
 *
 * Optional returns for an fhandle_t and fattr_t corresponding to the new file.
 *
 * Returns 0 if successful, non-zero if error.
 */
int nfs_create_sync(const char *name, uint32_t mode, size_t sz, fhandle_t *ret_fh, fattr_t *ret_fattr);

#endif /* _NFS_SYNC_H_ */
