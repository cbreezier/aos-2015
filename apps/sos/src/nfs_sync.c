#include <sel4/sel4.h>
#include <bits/errno.h>
#include <string.h>

#include "thread.h"
#include "nfs_sync.h"
#include "network.h"

struct token {
    seL4_CPtr async_ep;
    fhandle_t fh;
    fattr_t fattr;
    enum nfs_stat status;

    /* For read and write */
    void *sos_buf;
    int count;
    bool finished;

    /* For readdir */
    nfscookie_t cookie;
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

static void nfs_lookup_cb(uintptr_t token, enum nfs_stat status, fhandle_t *fh, fattr_t *fattr) {
    printf("lookup cb called\n");
    struct token *t = (struct token*)token;
    t->fh = *fh;
    t->fattr = *fattr;
    t->status = status;

    seL4_Notify(t->async_ep, 0);
}

int nfs_lookup_sync(const char *name, fhandle_t *ret_fh, fattr_t *ret_fattr) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    printf("async call lookup\n");
    enum rpc_stat res = nfs_lookup(&mnt_point, name, nfs_lookup_cb, (uintptr_t)(&t));
    printf("done async call\n");
    int err = rpc_stat_to_err(res);
    if (err) {
        return err;
    }

    seL4_Wait(t.async_ep, NULL);
    err = nfs_stat_to_err(t.status);
    if (err) {
        return err;
    }

    if (ret_fh != NULL) *ret_fh = t.fh;
    if (ret_fattr != NULL) *ret_fattr = t.fattr;

    return 0;
}

static void nfs_read_cb(uintptr_t token, enum nfs_stat status, fattr_t *fattr, int count, void *data) {
    struct token *t = (struct token*)token;
    t->status = status;
    t->fattr = *fattr;
    if (!count) {
        t->finished = true;
    }

    memcpy(t->sos_buf + t->count, data, count);
    t->count += count;
    
    seL4_Notify(t->async_ep, 0);
}

/* Returns number of bytes read. Returns -error upon error */
int nfs_read_sync(struct file_t *file, uint32_t offset, void *sos_buf, size_t nbytes) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    t.sos_buf = sos_buf;
    t.count = 0;
    t.finished = false;
    
    while (!t.finished && t.count < nbytes) {
        printf("t.count read %d\n", t.count);
        enum rpc_stat res = nfs_read(&file->fh, offset + t.count, nbytes - t.count, nfs_read_cb, (uintptr_t)(&t));
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
    
    return t.count;
}

static void nfs_write_cb(uintptr_t token, enum nfs_stat status, fattr_t *fattr, int count) {
    struct token *t = (struct token*)token;
    t->status = status;
    t->fattr = *fattr;
    if (!count) {
        t->finished = true;
    }

    t->count += count;
    
    seL4_Notify(t->async_ep, 0);
}

/* Returns number of bytes written. Returns -error upon error */
int nfs_write_sync(struct file_t *file, uint32_t offset, void *sos_buf, size_t nbytes) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    t.sos_buf = sos_buf;
    t.count = 0;
    t.finished = false;
    
    while (!t.finished && t.count < nbytes) {
        enum rpc_stat res = nfs_write(&file->fh, offset + t.count, nbytes - t.count, sos_buf + t.count, nfs_write_cb, (uintptr_t)(&t));
        int err = rpc_stat_to_err(res);
        if (err) {
            return -err;
        }

        seL4_Wait(t.async_ep, NULL);
        err = nfs_stat_to_err(t.status);
        if (err) {
            return -err;
        }
    }
    
    return t.count;
}

static void nfs_readdir_cb(uintptr_t token, enum nfs_stat status, int num_files, char *file_names[], nfscookie_t nfscookie) {
    printf("readdir cb called\n");
    struct token *t = (struct token*)token;
    t->status = status;
    t->cookie = nfscookie;

    char **dst = (char **)t->sos_buf;

    for (int i = 0; i < num_files; ++i) {
        printf("    File %d name %s\n", i, file_names[i]);
        strncpy(dst[t->count + i], file_names[i], NAME_MAX);
        dst[t->count + i][NAME_MAX - 1] = '\0';
    }
    t->count += num_files;
    printf("readdir cb finished copying %d files\n", num_files);

    seL4_Notify(t->async_ep, 0);
}

int nfs_readdir_sync(void *sos_buf, int *num_files) {
    printf("readdir sync called\n");
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    t.sos_buf = sos_buf;
    t.count = 0;
    t.cookie = 0;

    do {
        printf("readdir calling cb\n");
        enum rpc_stat res = nfs_readdir(&mnt_point, t.cookie, nfs_readdir_cb, (uintptr_t)(&t));
        int err = rpc_stat_to_err(res);
        if (res) {
            return err;
        }

        seL4_Wait(t.async_ep, NULL);
        printf("cookie is %d\n", t.cookie);
        err = nfs_stat_to_err(t.status);
        if (err) {
            return err;
        }
    } while (t.cookie);

    if (num_files != NULL) *num_files = t.count;
    printf("readdir sync done\n");

    return 0;
}

void nfs_create_cb(uintptr_t token, enum nfs_stat status, fhandle_t *fh, fattr_t *fattr) {
    struct token *t = (struct token*)token;
    t->fh = *fh;
    t->fattr = *fattr;
    t->status = status;

    seL4_Notify(t->async_ep, 0);
}

int nfs_create_sync(const char *name, uint32_t mode, fhandle_t *ret_fh, fattr_t *ret_fattr) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    sattr_t sattr;
    sattr.mode = mode;
    sattr.uid = -1;
    sattr.gid = -1;
    sattr.size = 0;
    sattr.atime.tv_sec = -1;
    sattr.atime.tv_usec = -1;
    sattr.mtime.tv_sec = -1;
    sattr.mtime.tv_usec = -1;

    printf("async call create\n");
    enum rpc_stat res = nfs_create(&mnt_point, name, &sattr, nfs_create_cb, (uintptr_t)(&t));
    printf("done async create call\n");
    int err = rpc_stat_to_err(res);
    if (err) {
        return err;
    }

    seL4_Wait(t.async_ep, NULL);
    err = nfs_stat_to_err(t.status);
    if (err) {
        return err;
    }

    if (ret_fh != NULL) *ret_fh = t.fh;
    if (ret_fattr != NULL) *ret_fattr = t.fattr;

    return 0;
}
