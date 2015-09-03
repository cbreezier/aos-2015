#ifndef _NFS_SYNC_H_
#define _NFS_SYNC_H_

#include <nfs/nfs.h>

int nfs_lookup_sync(const char *name, fhandle_t *ret_fh, fattr_t *ret_fattr);

int nfs_read_sync(struct file_t *file, uint32_t offset, void *sos_buf, size_t nbytes);

int nfs_write_sync(struct file_t *file, uint32_t offset, void *sos_buf, size_t nbytes);

#endif /* _NFS_SYNC_H_ */
