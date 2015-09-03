#include "nfs_sync.h"
#include "network.h"

struct token {
    seL4_CPtr async_ep;
    fhandle_t fh;
    fattr_t fattr;
    enum nfs_stat status;
};

static int rpc_stat_to_err(enum rpc_stat stat) {
    switch(stat) {
        case RPC_OK:
            return 0;
        case RPCERR_NOMEM:
            return ENOMEM;
        default:
            return ENETDOWN;
    }

    return 0;
}

static int nfs_stat_to_err(enum nfs_stat stat) {
    switch(stat) {
        case NFS_OK:
            return 0;
        case NFSERR_PERM:
            return EACCES;
        case NFSERR_NOENT:
            return ENOENT;
        case NFSERR_ACCES:
            return EACCES;
        case NFSERR_NOTDIR:
            return ENOTDIR;
        case NFSERR_NAMETOOLONG:
            return ENAMETOOLONG;
        case NFSERR_FBIG:
            return EOVERFLOW;
        default:
            return EFAULT;
    }

    return 0;
}

void nfs_lookup_cb(uintptr_t token, enum nfs_stat status, fhandle_t *fh, fattr_t *fattr) {
    struct token *t = (struct token*)token;
    t->fh = *fh;
    t->fattr = *fattr;
    t->status = status;

    seL4_Notify(t->async_ep);
}

int nfs_lookup_sync(const char *name, fhandle_t *ret_fh, fattr_t *ret_fattr) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    enum rpc_stat res = nfs_lookup(mnt_point, name, nfs_lookup_cb, &t);
    int err = rpc_stat_to_err(res);
    if (err) {
        return err;
    }

    seL4_Wait(t.async_ep, NULL);
    err = nfs_stat_to_err(t.status);
    if (err) {
        return err;
    }

    *ret_fh = t.fh;
    *ret_fattr = t.fattr;

    return 0;
}
