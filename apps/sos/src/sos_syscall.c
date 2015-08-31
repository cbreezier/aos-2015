#include "sos_syscall.h"
#include <sys/mman.h>
#include <vmem_layout.h>
#include <clock/clock.h>
#include <limits.h>
#include <copy.h>
#include <stdlib.h>
#include <string.h>
#include <console.h>
#include <syscall.h>

void sos_mmap2(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
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

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 8);
    seL4_SetMR(0, err);
    seL4_SetMR(1, insert_location);

    seL4_Send(reply_cap, reply);

    cspace_free_slot(cur_cspace, reply_cap);
}

void sos_munmap(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    void *addr = (void*)seL4_GetMR(1); 
    size_t length = (size_t)seL4_GetMR(2); 

    (void) length;
    (void) num_args;

    int err = as_remove_region(proc->as, (seL4_Word)addr);
    seL4_SetMR(0, err);

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 4);

    seL4_Send(reply_cap, reply);

    cspace_free_slot(cur_cspace, reply_cap);
}

void reply_user(uint32_t id, void *data) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 1);

    seL4_SetMR(0, 0);

    seL4_CPtr reply_cap = *((seL4_CPtr*)data);
    seL4_Send(reply_cap, reply);

    cspace_free_slot(cur_cspace, reply_cap);

    free((seL4_CPtr*)data);

}   

void sos_nanosleep(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    (void) proc;
    (void) num_args;

    uint64_t delay = (uint64_t) seL4_GetMR(1);
    delay *= 1000ull;

    seL4_CPtr *data = malloc(sizeof(seL4_CPtr));
    uint32_t err = 0;
    if (data != NULL) {
        *data = reply_cap;
        err = register_timer(delay, reply_user, (void*)data);
    }
    if (data == NULL || err == 0) {
        seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 4);

        seL4_SetMR(0, EFAULT);

        seL4_Send(reply_cap, reply);
    
        cspace_free_slot(cur_cspace, reply_cap);

        free(data);
    }
}

void sos_clock_gettime(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    (void) proc;
    (void) num_args;

    uint64_t timestamp = time_stamp();

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 3*4);

    seL4_SetMR(0, 0);
    seL4_SetMR(1, (seL4_Word)(timestamp & 0x00000000FFFFFFFF));
    seL4_SetMR(2, (seL4_Word)((timestamp >> 32) & 0x00000000FFFFFFFF));

    seL4_Send(reply_cap, reply);

    cspace_free_slot(cur_cspace, reply_cap);
}

void sos_brk(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    (void) num_args;
    
    seL4_Word new_top = seL4_GetMR(1);        
    seL4_Word new_top_align = ((new_top - 1) / PAGE_SIZE + 1) * PAGE_SIZE;

    struct region_entry *heap = proc->as->heap_region;

    int error = ENOMEM;

    if ((heap->next == NULL || new_top_align <= heap->next->start) && new_top_align > heap->start) {
        size_t new_size = new_top_align - heap->start;
        heap->size = new_size;
        error = 0;
    }

    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2*4);

    seL4_SetMR(0, error);
    seL4_SetMR(1, new_top_align);

    seL4_Send(reply_cap, reply);

    cspace_free_slot(cur_cspace, reply_cap);
}

void sos_open(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    (void) num_args;

    int err = 0;
    int fd = 0;

    /* Do copyin to get path */
    void *user_path = (void*) seL4_GetMR(1);
    char *path = malloc(NAME_MAX * sizeof(char));
    if (path == NULL) {
        err = ENOMEM;
        goto sos_open_end;
    }

    err = copyinstring(proc, path, user_path, NAME_MAX);
    if (err) {
        goto sos_open_end;
    }

    /* TODO M5: SUPPORT FLAGS */
    int mode = (int) seL4_GetMR(2);
    (void)mode;

    int open_entry = -1;
    int existing_location = -1;

    sync_acquire(open_files_lock);
    for (int i = OPEN_FILE_MAX; i >= 0; --i) {
        if (open_files[i].ref_count > 0) {
            if (strcmp(open_files[i].file_obj.name, path) == 0) {
                existing_location = i;
                break; 
            }
        } else {
            open_entry = i;
        }
    }

    if (existing_location == -1 && open_entry == -1) {
        err = ENFILE;
        goto sos_open_end;
    }

    fd = proc->files_head_free;
    /* Update next_free, free_head, free_tail */
    if (fd == 0) {
        err = EMFILE;
        goto sos_open_end;
    }
    proc->files_head_free = proc->proc_files[fd].next_free;

    proc->proc_files[fd].offset = 0;

    if (existing_location == -1) {
        proc->proc_files[fd].open_file_idx = open_entry;
        proc->proc_files[fd].used = true;

        open_files[open_entry].ref_count = 1;
        strcpy(open_files[open_entry].file_obj.name, path);
        /* Set function pointers */
        assert(strcmp(path, "console") == 0);
        open_files[open_entry].file_obj.read = console_read;
        open_files[open_entry].file_obj.write = console_write;
    } else {
        proc->proc_files[fd].open_file_idx = existing_location;

        open_files[existing_location].ref_count++;
    }
    
sos_open_end:
    sync_release(open_files_lock);
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2*4);

    seL4_SetMR(0, err);
    seL4_SetMR(1, (seL4_Word)fd);

    seL4_Send(reply_cap, reply);

    cspace_free_slot(cur_cspace, reply_cap);

    free(path);
}

void sos_close(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    (void) num_args;
}

void sos_read(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    (void) num_args;
    int fd = (int) seL4_GetMR(1);
    void *buf = (void*) seL4_GetMR(2);
    size_t nbytes = (size_t) seL4_GetMR(3);

    int err = 0;
    int nread = 0;
    char *sos_buffer = NULL;

    sync_acquire(open_files_lock);
    struct fd_entry *fd_entry = &proc->proc_files[fd];
    if (!fd_entry->used) {
        err = EBADF;
        goto sos_read_end;
    }

    struct file_entry *fe = &open_files[fd_entry->open_file_idx];
    if (fe->ref_count == 0) {
        assert(!"File descriptor did not match valid open file entry - sos read");
    }

    struct file_t *file = &fe->file_obj;
    sos_buffer = malloc(nbytes * sizeof(char));
    if (sos_buffer == NULL) {
        err = ENOMEM;
        goto sos_read_end;
    }
    nread = file->read(file, fd_entry->offset, sos_buffer, nbytes);
    err = copyout(proc, buf, sos_buffer, nread);
    if (err) {
        goto sos_read_end;
    }

sos_read_end:
    sync_release(open_files_lock);
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(seL4_NoFault, 0, 0, 2*4);

    seL4_SetMR(0, err);
    seL4_SetMR(1, nread);
    
    seL4_Send(reply_cap, reply);

    cspace_free_slot(cur_cspace, reply_cap);

    free(sos_buffer);
}

void sos_write(sos_process_t *proc, seL4_CPtr reply_cap, int num_args) {
    (void) num_args;
    int fd = (int) seL4_GetMR(1);
    void *buf = (void*) seL4_GetMR(2);
    size_t nbytes = (size_t) seL4_GetMR(3);

    int err = 0;
    int nwrite = 0;
    char *sos_buffer = NULL;

    sync_acquire(open_files_lock);
    struct fd_entry *fd_entry = &proc->proc_files[fd];
    if (!fd_entry->used) {
        err = EBADF;
        goto sos_write_end;
    }

    struct file_entry *fe = &open_files[fd_entry->open_file_idx];
    if (fe->ref_count == 0) {
        assert(!"File descriptor did not match valid open file entry - sos write");
    }

    struct file_t *file = &fe->file_obj;
    sos_buffer = malloc(nbytes * sizeof(char));
    if (sos_buffer == NULL) {
        err = ENOMEM;
        goto sos_write_end;
    }
    err = copyin(proc, sos_buffer, buf, nbytes);
    if (err) {
        goto sos_write_end;
    }
    nwrite = file->write(file, fd_entry->offset, sos_buffer, nbytes);

sos_write_end:
    sync_release(open_files_lock);
    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2*4);

    seL4_SetMR(0, err);
    seL4_SetMR(1, (seL4_Word)nwrite);

    seL4_Send(reply_cap, reply);

    cspace_free_slot(cur_cspace, reply_cap);

    free(sos_buffer);

}
