#ifndef _NFS_SYNC_H_
#define _NFS_SYNC_H_

#include <nfs/nfs.h>
#include "file.h"
#include "proc.h"

int nfs_lookup_sync(const char *name, fhandle_t *ret_fh, fattr_t *ret_fattr);

int nfs_read_sync(process_t *proc, struct file_t *file, uint32_t offset, void *sos_buf, size_t nbytes);

int nfs_write_sync(process_t *proc, struct file_t *file, uint32_t offset, void *sos_buf, size_t nbytes);

int nfs_readdir_sync(void *sos_buf, int *num_files);

int nfs_create_sync(const char *name, uint32_t mode, size_t sz, fhandle_t *ret_fh, fattr_t *ret_fattr);

#endif /* _NFS_SYNC_H_ */
