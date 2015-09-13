#include <sel4/sel4.h>
#include <bits/errno.h>
#include <string.h>

#include "thread.h"
#include "nfs_sync.h"
#include "network.h"
#include "copy.h"
#include "frametable.h"

struct token {
    seL4_CPtr async_ep;
    fhandle_t fh;
    fattr_t fattr;
    enum nfs_stat status;

    /* For read and write */
    void *sos_buf;
    void *usr_buf;
    int count;
    bool finished;
    int err;

    process_t *proc;

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
        case NFSERR_IO:
            return EIO;
        case NFSERR_NXIO:
            return ENXIO;
        case NFSERR_ACCES:
            return EACCES;
        case NFSERR_EXIST:
            return EEXIST;
        case NFSERR_NODEV:
            return ENODEV;
        case NFSERR_NOTDIR:
            return ENOTDIR;
        case NFSERR_ISDIR:
            return EISDIR;
        case NFSERR_FBIG:
            return EFBIG;
        case NFSERR_NOSPC:
            return ENOSPC;
        case NFSERR_ROFS:
            return EROFS;
        case NFSERR_NAMETOOLONG:
            return ENAMETOOLONG;
        case NFSERR_NOTEMPTY:
            return ENOTEMPTY;
        case NFSERR_DQUOT:
            return EDQUOT;
        case NFSERR_STALE:
            return EBADF;
        case NFSERR_WFLUSH:
            return EIO;
        case NFSERR_COMM:
            return ENETDOWN;
        default:
            return EFAULT;
    }

    return 0;
}

static void nfs_lookup_cb(uintptr_t token, enum nfs_stat status, fhandle_t *fh, fattr_t *fattr) {
    struct token *t = (struct token*)token;
    t->fh = *fh;
    t->fattr = *fattr;
    t->status = status;

    seL4_Notify(t->async_ep, 0);
}

int nfs_lookup_sync(const char *name, fhandle_t *ret_fh, fattr_t *ret_fattr) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    enum rpc_stat res = nfs_lookup(&mnt_point, name, nfs_lookup_cb, (uintptr_t)(&t));
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

    t->err = copyout(t->proc, t->usr_buf + t->count, data, count);
    t->count += count;
    
    seL4_Notify(t->async_ep, 0);
}

/* Returns number of bytes read. Returns -error upon error */
int nfs_read_sync(process_t *proc, struct file_t *file, uint32_t offset, void *usr_buf, size_t nbytes) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    t.usr_buf = usr_buf;
    t.proc = proc;
    t.count = 0;
    t.finished = false;
    t.err = 0;
    
    while (!t.finished && t.count < nbytes) {
        size_t to_read = nbytes - t.count < PAGE_SIZE ? nbytes - t.count : PAGE_SIZE;
        enum rpc_stat res = nfs_read(&file->fh, offset + t.count, to_read, nfs_read_cb, (uintptr_t)(&t));
        if (t.err) {
            return -t.err;
        }
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

static void nfs_sos_read_cb(uintptr_t token, enum nfs_stat status, fattr_t *fattr, int count, void *data) {
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

int nfs_sos_read_sync(fhandle_t fh, uint32_t offset, void *sos_buf, size_t nbytes) {
    assert(nbytes <= PAGE_SIZE);
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    t.sos_buf = sos_buf;
    t.count = 0;
    t.finished = false;
    t.err = 0;
    
    while (!t.finished && t.count < nbytes) {
        enum rpc_stat res = nfs_read(&fh, offset + t.count, nbytes - t.count, nfs_sos_read_cb, (uintptr_t)(&t));
        if (t.err) {
            return -t.err;
        }
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
int nfs_write_sync(process_t *proc, struct file_t *file, uint32_t offset, void *usr_buf, size_t nbytes) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    t.count = 0;
    t.finished = false;

    if (!user_buf_in_region(proc, usr_buf, nbytes)) {
        return -EFAULT;
    }
 
    while (!t.finished && nbytes > 0) {
        seL4_Word svaddr;
        size_t to_write = 0;
        int err = user_buf_to_sos(proc, usr_buf, nbytes, &svaddr, &to_write);
        if (err) {
            return -err;
        }
        int count_before = t.count;
        enum rpc_stat res = nfs_write(&file->fh, offset + t.count, to_write, (void*)(svaddr), nfs_write_cb, (uintptr_t)(&t));
        err = rpc_stat_to_err(res);
        if (err) {
            frame_change_swappable(svaddr, true);
            return -err;
        }
        seL4_Wait(t.async_ep, NULL);
        frame_change_swappable(svaddr, true);

        err = nfs_stat_to_err(t.status);
        if (err) {
            return -err;
        }
        int written = t.count - count_before;
        usr_buf += written;
        nbytes -= written;
    }
    
    return t.count;
}

/* Returns number of bytes written. Returns -error upon error */
int nfs_sos_write_sync(fhandle_t fh, uint32_t offset, void *sos_buf, size_t nbytes) {
    assert(nbytes <= PAGE_SIZE && "nfs_sos_write_sync only handles writing at most PAGE_SIZE bytes");
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    t.count = 0;
    t.finished = false;
 
    while (!t.finished && nbytes > 0) {
        int count_before = t.count;
        enum rpc_stat res = nfs_write(&fh, offset + t.count, nbytes, (void*)sos_buf, nfs_write_cb, (uintptr_t)(&t));
        int err = rpc_stat_to_err(res);
        if (err) {
            return -err;
        }
        seL4_Wait(t.async_ep, NULL);

        err = nfs_stat_to_err(t.status);
        if (err) {
            return -err;
        }
        int written = t.count - count_before;
        sos_buf += written;
        nbytes -= written;
    }
    
    return t.count;
}

static void nfs_readdir_cb(uintptr_t token, enum nfs_stat status, int num_files, char *file_names[], nfscookie_t nfscookie) {
    struct token *t = (struct token*)token;
    t->status = status;
    t->cookie = nfscookie;

    char **dst = (char **)t->sos_buf;

    for (int i = 0; i < num_files; ++i) {
        if (t->count+i >= FILES_PER_DIR) {
            break;
        }
        strncpy(dst[t->count + i], file_names[i], NAME_MAX);
        dst[t->count + i][NAME_MAX - 1] = '\0';
    }
    t->count += num_files;

    seL4_Notify(t->async_ep, 0);
}

int nfs_readdir_sync(void *sos_buf, int *num_files) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    t.sos_buf = sos_buf;
    t.count = 0;
    t.cookie = 0;

    do {
        enum rpc_stat res = nfs_readdir(&mnt_point, t.cookie, nfs_readdir_cb, (uintptr_t)(&t));
        int err = rpc_stat_to_err(res);
        if (res) {
            return err;
        }
        seL4_Wait(t.async_ep, NULL);
        err = nfs_stat_to_err(t.status);
        if (err) {
            return err;
        }
    } while (t.cookie);

    if (num_files != NULL) *num_files = t.count;

    return 0;
}

void nfs_create_cb(uintptr_t token, enum nfs_stat status, fhandle_t *fh, fattr_t *fattr) {
    struct token *t = (struct token*)token;
    t->fh = *fh;
    t->fattr = *fattr;
    t->status = status;

    seL4_Notify(t->async_ep, 0);
}

int nfs_create_sync(const char *name, uint32_t mode, size_t sz, fhandle_t *ret_fh, fattr_t *ret_fattr) {
    struct token t;
    t.async_ep = get_cur_thread()->wakeup_async_ep;
    sattr_t sattr;
    sattr.mode = mode;
    sattr.uid = -1;
    sattr.gid = -1;
    sattr.size = sz;
    sattr.atime.tv_sec = -1;
    sattr.atime.tv_usec = -1;
    sattr.mtime.tv_sec = -1;
    sattr.atime.tv_usec = -1;

    enum rpc_stat res = nfs_create(&mnt_point, name, &sattr, nfs_create_cb, (uintptr_t)(&t));
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
