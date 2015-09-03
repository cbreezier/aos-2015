#ifndef _NFS_SYNC_H_
#define _NFS_SYNC_H_

#include <nfs/nfs.h>

void nfs_lookup_cb(uintptr_t token, enum nfs_stat status, fhandle_t *fh, fattr_t *fattr);

int nfs_lookup_sync(const char *name, fhandle_t *ret_fh, fattr_t *ret_fattr);

#endif /* _NFS_SYNC_H_ */
