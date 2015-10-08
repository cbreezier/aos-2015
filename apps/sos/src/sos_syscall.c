#include "sos_syscall.h"
#include <sys/mman.h>
#include <sys/debug.h>
#include <vmem_layout.h>
#include <clock/clock.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <nfs/nfs.h>
#include <sys/stat.h>
#include "console.h"
#include "thread.h"
#include "nfs_sync.h"
#include "copy.h"
#include "alloc_wrappers.h"

seL4_MessageInfo_t sos_null(process_t *proc, int num_args) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, 0);

    return reply;
}

seL4_MessageInfo_t sos_mmap2(process_t *proc, int num_args) {
    void *addr = (void*)seL4_GetMR(1); 
    size_t length = (size_t)seL4_GetMR(2); 
    int prot = (int)seL4_GetMR(3); 
    int flags = (int)seL4_GetMR(4); 
    int fd = (int)seL4_GetMR(5); 
    off_t offset = (off_t)seL4_GetMR(6); 
   
    (void) addr; 
    (void) flags;
    (void) fd;
    (void) offset;
    (void) num_args;

    seL4_Word insert_location;

    int err = as_search_add_region(proc->as, PROCESS_VMEM_START, length, prot & PROT_WRITE, prot & PROT_READ, prot & PROT_EXEC, &insert_location);

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, err);
    seL4_SetMR(1, insert_location);

    return reply;
}

seL4_MessageInfo_t sos_munmap(process_t *proc, int num_args) {
    void *addr = (void*)seL4_GetMR(1); 
    size_t length = (size_t)seL4_GetMR(2); 

    (void) length;
    (void) num_args;

    int err = as_remove_region(proc, (seL4_Word)addr);
    seL4_SetMR(0, err);

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);

    return reply;
}

void sos_nanosleep_notify(uint32_t id, void *data) {
    seL4_Notify(*((seL4_CPtr*)data), 0);
}   

seL4_MessageInfo_t sos_nanosleep(process_t *proc, int num_args) {
    (void) proc;
    (void) num_args;

    uint64_t delay = (uint64_t) seL4_GetMR(1);
    delay *= 1000ull;

    seL4_CPtr *data = kmalloc(sizeof(seL4_CPtr));
    uint32_t err = 0;
    if (data != NULL) {
        seL4_CPtr async_ep = get_cur_thread()->wakeup_async_ep;
        *data = async_ep;
        err = register_timer(delay, sos_nanosleep_notify, (void*)data, &proc->timer_sleep_node);
        seL4_Wait(async_ep, NULL);
    } else {
        err = ENOMEM;
    }

    if (data) {
        kfree(data);
    }

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);

    seL4_SetMR(0, err);

    return reply;
}

seL4_MessageInfo_t sos_clock_gettime(process_t *proc, int num_args) {
    (void) proc;
    (void) num_args;

    uint64_t timestamp = time_stamp();

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 3);

    seL4_SetMR(0, 0);
    seL4_SetMR(1, (seL4_Word)(timestamp & 0x00000000FFFFFFFF));
    seL4_SetMR(2, (seL4_Word)((timestamp >> 32) & 0x00000000FFFFFFFF));

    return reply;
}

seL4_MessageInfo_t sos_brk(process_t *proc, int num_args) {
    (void) num_args;
    
    seL4_Word new_top = seL4_GetMR(1);        
    seL4_Word new_top_align = ((new_top - 1) / PAGE_SIZE + 1) * PAGE_SIZE;

    struct region_entry *heap = proc->as->heap_region;

    int err = ENOMEM;

    if ((heap->next == NULL || new_top_align <= heap->next->start) && new_top_align > heap->start) {
        size_t new_size = new_top_align - heap->start;
        heap->size = new_size;
        err = 0;
    }

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);

    seL4_SetMR(0, err);
    seL4_SetMR(1, new_top_align);

    return reply;
}

static fmode_t nfs_mode_to_sos(uint32_t mode) {
    fmode_t ret = 0;
    if (mode & S_IRUSR) {
        ret |= FM_READ;
    }
    if (mode & S_IWUSR) {
        ret |= FM_WRITE;
    }
    if (mode & S_IXUSR) {
        ret |= FM_EXEC;
    }
    return ret;
}

static uint32_t sos_mode_to_nfs(fmode_t mode) {
    fmode_t ret = 0;
    if (mode & FM_READ) {
        ret |= S_IRUSR;
    }
    if (mode & FM_WRITE) {
        ret |= S_IWUSR;
    }
    if (mode & FM_EXEC) {
        ret |= S_IXUSR;
    }
    return ret;
}

seL4_MessageInfo_t sos_open(process_t *proc, int num_args) {
    (void) num_args;

    void *user_path = (void*) seL4_GetMR(1);
    fmode_t mode = (fmode_t) seL4_GetMR(2);

    int err = 0;
    int fd = -1;

    /* Do copyin to get path */
    char *path = kmalloc(NAME_MAX * sizeof(char));
    if (path == NULL) {
        err = ENOMEM;
        goto sos_open_end;
    }

    err = copyinstring(proc, path, user_path, NAME_MAX);
    if (err) {
        goto sos_open_end;
    }
    path[NAME_MAX-1] = 0;

    bool exists = false;
    int open_entry = -1;

    sync_acquire(open_files_lock);
    for (int i = OPEN_FILE_MAX; i >= 0; --i) {
        if (open_files[i].ref_count > 0) {
            if (strcmp(open_files[i].file_obj.name, path) == 0) {
                exists = true;
                open_entry = i;
                break; 
            }
        } else {
            open_entry = i;
        }
    }

    if (!exists && open_entry == -1) {
        sync_release(open_files_lock);
        err = ENFILE;
        goto sos_open_end;
    }

    fd = proc->files_head_free;
    if (fd == -1) {
        sync_release(open_files_lock);
        err = EMFILE;
        goto sos_open_end;
    }

    if (!exists) {
        open_files[open_entry].ref_count = 1;
        sync_release(open_files_lock);
        /* Set function pointers */
        if (strcmp(path, "console") == 0) {
            open_files[open_entry].file_obj.read = console_read;
            open_files[open_entry].file_obj.write = console_write;
        } else {
            fhandle_t fh;
            fattr_t fattr;
            err = nfs_lookup_sync(path, &fh, &fattr);

            if (err == ENOENT/* && (mode & O_CREAT)*/) {
                uint32_t nfs_mode = sos_mode_to_nfs(FM_WRITE | FM_READ);
                err = nfs_create_sync(path, nfs_mode, 0, &fh, &fattr);
            }
            if (err) {
                goto sos_open_end;
            }

            fmode_t file_mode = nfs_mode_to_sos(fattr.mode);
            if ((!(file_mode & FM_READ) && (mode & FM_READ)) ||
                (!(file_mode & FM_WRITE) && (mode & FM_WRITE)) ||
                (!(file_mode & FM_EXEC) && (mode & FM_EXEC))) {
                err = EACCES;
                goto sos_open_end;
            }

            open_files[open_entry].file_obj.fh = fh;

            open_files[open_entry].file_obj.read = nfs_read_sync;
            open_files[open_entry].file_obj.write = nfs_write_sync;
        }

        strcpy(open_files[open_entry].file_obj.name, path);

    } else {
        open_files[open_entry].ref_count++;
        sync_release(open_files_lock);
    }
    
    proc->files_head_free = proc->proc_files[fd].next_free;

    proc->proc_files[fd].open_file_idx = open_entry;
    proc->proc_files[fd].used = true;
    proc->proc_files[fd].offset = 0;
    proc->proc_files[fd].mode = mode;

    
sos_open_end:
    if (path) {
        kfree(path);
    }
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);

    seL4_SetMR(0, err);
    seL4_SetMR(1, (seL4_Word)fd);

    return reply;

}

seL4_MessageInfo_t sos_close(process_t *proc, int num_args) {
    (void) num_args;

    int err = 0;
    int fd = seL4_GetMR(1);
    
    if (fd < 0 || fd >= OPEN_FILE_MAX) {
        err = EBADF;
        goto sos_close_end;
    }
    struct fd_entry *fd_entry = &proc->proc_files[fd];
    if (!fd_entry->used) {
        err = EBADF;
        goto sos_close_end;
    }

    fd_entry->used = false;

    sync_acquire(open_files_lock);
    open_files[fd_entry->open_file_idx].ref_count--;
    sync_release(open_files_lock);

    /* Add fd back to available fds */
    if (proc->files_head_free == -1) {
        proc->files_head_free = fd;
    } else {
        proc->proc_files[proc->files_tail_free].next_free = fd;
    }
    proc->files_tail_free = fd;

    proc->proc_files[fd].next_free = -1;

sos_close_end:
    asm("nop");
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);

    seL4_SetMR(0, err);

    return reply;
}

seL4_MessageInfo_t sos_read(process_t *proc, int num_args) {
    (void) num_args;
    int fd = (int) seL4_GetMR(1);
    void *buf = (void*) seL4_GetMR(2);
    size_t nbytes = (size_t) seL4_GetMR(3);

    int err = 0;
    int nread = 0;

    if (fd < 0 || fd >= OPEN_FILE_MAX) {
        err = EBADF;
        goto sos_read_end;
    }

    struct fd_entry *fd_entry = &proc->proc_files[fd];
    if (!fd_entry->used) {
        err = EBADF;
        goto sos_read_end;
    }
    
    if (!(fd_entry->mode & FM_READ)) {
        err = EBADF;
        goto sos_read_end;
    }

    struct file_entry *fe = &open_files[fd_entry->open_file_idx];
    if (fe->ref_count == 0) {
        assert(!"File descriptor did not match valid open file entry - sos read");
    }

    struct file_t *file = &fe->file_obj;
    nread = file->read(proc, &file->fh, fd_entry->offset, buf, nbytes);
    if (nread < 0) {
        err = -nread;
        goto sos_read_end;
    }
    fd_entry->offset += nread;

sos_read_end:
    asm("nop");
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 2);

    seL4_SetMR(0, err);
    seL4_SetMR(1, nread);
    
    return reply;
}

seL4_MessageInfo_t sos_write(process_t *proc, int num_args) {
    (void) num_args;
    int fd = (int) seL4_GetMR(1);
    void *buf = (void*) seL4_GetMR(2);
    size_t nbytes = (size_t) seL4_GetMR(3);

    int err = 0;
    int nwrite = 0;

    if (fd < 0 || fd >= OPEN_FILE_MAX) {
        err = EBADF;
        goto sos_write_end;
    }

    struct fd_entry *fd_entry = &proc->proc_files[fd];
    if (!fd_entry->used) {
        err = EBADF;
        goto sos_write_end;
    }

    if (!(proc->proc_files[fd].mode & FM_WRITE)) {
        err = EBADF;
        goto sos_write_end;
    }


    struct file_entry *fe = &open_files[fd_entry->open_file_idx];
    if (fe->ref_count == 0) {
        assert(!"File descriptor did not match valid open file entry - sos write");
    }

    struct file_t *file = &fe->file_obj;
    nwrite = file->write(proc, &file->fh, fd_entry->offset, buf, nbytes);
    if (nwrite < 0) {
        err = -nwrite;
        goto sos_write_end;
    }
    fd_entry->offset += nwrite;

sos_write_end:
    asm("nop");
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);

    seL4_SetMR(0, err);
    seL4_SetMR(1, (seL4_Word)nwrite);

    return reply;

}


seL4_MessageInfo_t sos_stat(process_t *proc, int num_args) {
    (void) num_args;

    int err = 0;

    void *user_path = (void*) seL4_GetMR(1);
    void *usr_buf = (void*) seL4_GetMR(2);

    char *path = kmalloc(NAME_MAX * sizeof(char));
    if (path == NULL) {
        err = ENOMEM;
        goto sos_stat_end;
    }

    err = copyinstring(proc, path, user_path, NAME_MAX);
    if (err) {
        goto sos_stat_end;
    }
    path[NAME_MAX-1] = 0;

    fhandle_t fh;
    fattr_t fattr;
    err = nfs_lookup_sync(path, &fh, &fattr); 

    if (err) {
        goto sos_stat_end;
    }

    sos_stat_t stat;
    stat.st_type = (st_type_t)fattr.type;
    stat.st_fmode = nfs_mode_to_sos(fattr.mode);
    stat.st_size = (unsigned)fattr.size;
    stat.st_ctime = fattr.ctime.tv_sec * 1000 + fattr.ctime.tv_usec / 1000;
    stat.st_atime = fattr.atime.tv_sec * 1000 + fattr.atime.tv_usec / 1000;
    err = copyout(proc, usr_buf, (void*)(&stat), sizeof(sos_stat_t));
    if (err) {
        goto sos_stat_end;
    }

sos_stat_end:
    if (path) {
        kfree(path);
    }
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);

    seL4_SetMR(0, err);

    return reply;
}

seL4_MessageInfo_t sos_getdents(process_t *proc, int num_args) {
    (void) num_args;

    int err = 0;

    int pos = (int)seL4_GetMR(1);
    void *usr_buf= (void*)seL4_GetMR(2);
    size_t usr_buf_sz = (size_t)seL4_GetMR(3);

    char **dir_entries = NULL;
    size_t file_name_size = 0;

    if (pos < 0 || pos >= FILES_PER_DIR) {
        err = EFAULT;
        goto sos_getdents_end;
    }

    dir_entries = kmalloc(sizeof(char*)*FILES_PER_DIR);
    if (dir_entries == NULL) {
        err = ENOMEM;
        goto sos_getdents_end;
    }
    for (int i = 0; i < FILES_PER_DIR; ++i) {
        dir_entries[i] = kmalloc(sizeof(char)*NAME_MAX);
        if (dir_entries[i] == NULL) {
            err = ENOMEM;
            goto sos_getdents_end;
        }
    }

    int num_files = 0;
    err = nfs_readdir_sync((void*)dir_entries, &num_files);
    dir_entries[FILES_PER_DIR-1][0] = '\0';
    if (err) {
        goto sos_getdents_end;
    }

    if (num_files <= pos) {
        goto sos_getdents_end;
    }

    file_name_size = strlen(dir_entries[pos]);
    if (file_name_size >= usr_buf_sz) {
        err = EINVAL;
        goto sos_getdents_end;
    }

    err = copyoutstring(proc, usr_buf, dir_entries[pos], usr_buf_sz);
    if (err) {
        goto sos_getdents_end;
    }

sos_getdents_end:
    if (dir_entries != NULL) {
        for (int i = 0; i < FILES_PER_DIR; ++i) {
            if (dir_entries[i] == NULL) {
                break;
            }
            kfree(dir_entries[i]);
        }
        kfree(dir_entries);
    }
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);

    seL4_SetMR(0, err);
    seL4_SetMR(1, file_name_size);

    return reply;
}

seL4_MessageInfo_t sos_execve(process_t *proc, int num_args) {
    (void) num_args;

    int err = 0;
    int pid = 0;

    void *usr_buf= (void*)seL4_GetMR(1);

    /* Do copyin to get path */
    char *path = kmalloc(NAME_MAX * sizeof(char));
    if (path == NULL) {
        err = ENOMEM;
        goto sos_execve_end;
    }

    err = copyinstring(proc, path, usr_buf, NAME_MAX);
    if (err) {
        goto sos_execve_end;
    }
    path[NAME_MAX-1] = 0;

    printf("sos_execve %s\n", path);

    pid = proc_create(proc->pid, path);
    if (pid < 0) {
        err = -pid;
        goto sos_execve_end;
    }
    
sos_execve_end:
    if (path) {
        kfree(path);   
    }
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);

    seL4_SetMR(0, err);
    seL4_SetMR(1, pid);

    return reply;
}

seL4_MessageInfo_t sos_getpid(process_t *proc, int num_args) {
    (void) num_args;

    pid_t pid = proc->pid;

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);

    seL4_SetMR(0, 0);
    seL4_SetMR(1, pid);

    return reply;
}

seL4_MessageInfo_t sos_ustat(process_t *proc, int num_args) {
    (void) num_args;
    
    int err = 0;
    int num_procs = 0;

    void *usr_buf = (void*)seL4_GetMR(1);
    int max_procs = (int)seL4_GetMR(2);

    if (max_procs < 0 || max_procs > MAX_PROCESSES) {
        err = EINVAL;
        goto sos_ustat_end;
    }

    for (int i = 0; i < MAX_PROCESSES && num_procs < max_procs; ++i) {
        pid_t pid = processes[i].pid;
        if (pid != -1) {
            sync_acquire(processes[i].proc_lock);
            sos_process_t sos_proc;
            sos_proc.pid = pid;
            sos_proc.size = processes[i].size;
            sos_proc.stime = processes[i].stime;
            strcpy(sos_proc.command, processes[i].command);
            sync_release(processes[i].proc_lock);

            err = copyout(proc, usr_buf, &sos_proc, sizeof(sos_process_t));
            if (err) {
                goto sos_ustat_end;
            }

            usr_buf += sizeof(sos_process_t);
            num_procs++;
        }
    }

sos_ustat_end:
    asm("nop");
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);

    seL4_SetMR(0, err);
    seL4_SetMR(1, num_procs);

    return reply;
}

seL4_MessageInfo_t sos_waitid(process_t *proc, int num_args) {
    (void) num_args;

    int err = 0;
    int pid = (int)seL4_GetMR(1);
    int pid_idx = pid & PROCESSES_MASK;
    if (pid < -1) {
        err = EINVAL;
        goto sos_waitid_end;
    }
    assert(pid <= PID_MAX);

    if (pid != -1) {
        sync_acquire(processes[pid_idx].proc_lock);
        /* Process we want to wait on does not exist */
        if (processes[pid_idx].pid != pid) {
            err = ECHILD;
            sync_release(processes[pid_idx].proc_lock);
            goto sos_waitid_end;
        }

        /* Process we want to wait on is not our child */
        if (processes[pid_idx].parent_proc != proc->pid) {
            err = ECHILD;
            sync_release(processes[pid_idx].proc_lock);
            goto sos_waitid_end;
        }
        sync_release(processes[pid_idx].proc_lock);
    }

    seL4_CPtr async_ep = get_cur_thread()->wakeup_async_ep;
    proc->wait_ep = async_ep;
    proc->wait_pid = pid;

    seL4_Wait(async_ep, NULL);
    /* 
     * At this point, our wait_pid is the child that exited (set by
     * exiting child).
     */

sos_waitid_end:
    asm("nop");
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);

    seL4_SetMR(0, err);
    seL4_SetMR(1, proc->wait_pid);

    return reply;
}

seL4_MessageInfo_t sos_kill(process_t *proc, int num_args) {
    (void) num_args;
    int err = 0;

    pid_t pid = seL4_GetMR(1);
    uint32_t pid_idx = pid & PROCESSES_MASK;

    process_t *to_kill = &processes[pid_idx];

    sync_acquire(to_kill->proc_lock);
    if (to_kill->pid != pid_idx || to_kill->zombie) {
        err = ESRCH;
        sync_release(to_kill->proc_lock);
        goto sos_kill_end;
    }
    if (to_kill->sos_thread_handling) {
        to_kill->zombie = true;
    } else {
        proc_exit(to_kill);
    }
    sync_release(to_kill->proc_lock);

sos_kill_end:
    asm("nop");
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);

    seL4_SetMR(0, err);

    return reply;
}

seL4_MessageInfo_t sos_exit(process_t *proc, int num_args) {
    (void) num_args;

    proc_exit(proc);
    
    seL4_MessageInfo_t reply;
    return reply; 
}

