#include "nfs_sync.h"
#include "network.h"
#include "file.h"

struct token {
    seL4_CPtr async_ep;
    fhandle_t fh;
    fattr_t fattr;
    enum nfs_stat status;

    /* For read and write */
    void *sos_buf;
    int count;
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

void nfs_read_cb(uintptr_t token, enum nfs_stat status, fattr_t *fattr, int count, void *data) {
    struct token *t = (struct token*)token;
    t->status = status;
    t->fattr = *fattr;

    memcpy(t->sos_buf + t->count, data, count);
    t->count += count;
    
    seL4_Notify(t->async_ep, 0);
}

int nfs_read_sync(struct file_t *file, uint32_t offset, void *sos_buf, size_t nbytes) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    t.sos_buf = sos_buf;
    t.count = 0;
    
    while (t.count < nbytes) {
        enum rpc_stat res = nfs_read(file->fh, offset + t.count, nbytes - t.count, nfs_read_cb, (uintptr_t)&t);
        int err = rpc_stat_to_err(res);
        if (err) {
            return err;
        }

        seL4_Wait(t.async_ep, NULL);
        err = nfs_stat_to_err(t.status);
        if (err) {
            return err;
        }
    }
    
    return 0;
}

void nfs_write_cb(uintptr_t token, enum nfs_stat status, fattr_t *fattr, int count) {
    struct token *t = (struct token*)token;
    t->status = status;
    t->fattr = *fattr;

    t->count += count;
    
    seL4_Notify(t->async_ep, 0);
}

int nfs_write_sync(struct file_t *file, uint32_t offset, void *sos_buf, size_t nbytes) {
    struct token t;
    t.async_ep = get_cur_thwrite()->wakeup_async_ep;
    t.sos_buf = sos_buf;
    t.count = 0;
    
    while (t.count < nbytes) {
        enum rpc_stat res = nfs_write(file->fh, offset + t.count, nbytes - t.count, nfs_write_cb, (uintptr_t)&t);
        int err = rpc_stat_to_err(res);
        if (err) {
            return err;
        }

        seL4_Wait(t.async_ep, NULL);
        err = nfs_stat_to_err(t.status);
        if (err) {
            return err;
        }
    }
    
    return 0;
}
