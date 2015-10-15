#include <string.h>
#include <stdio.h>
#include <sys/debug.h>
#include "copy.h"
#include "pagetable.h"
#include "frametable.h"
#include "swap.h"

static inline int min(int a, int b) {
    return a < b ? a : b;
}

bool usr_buf_in_region(process_t *proc, void *usr_buf, size_t buf_size, bool *ret_region_r, bool *ret_region_w) {
    /* Find region containing starting address usr_buf */
    struct region_entry *path_region = as_get_region(proc->as, usr_buf);
    if (path_region == NULL) {
        return false;
    }
    /* Check that the entire usr buffer lies within the region */
    seL4_Word region_end = path_region->start + path_region->size;
    if (sizeof(seL4_Word)*(region_end - (seL4_Word)usr_buf) < buf_size) {
        return false;
    }
    if (ret_region_r != NULL) *ret_region_r = path_region->r;
    if (ret_region_w != NULL) *ret_region_w = path_region->w;
    return true;
}

int usr_buf_to_sos(process_t *proc, void *usr_buf, size_t buf_size, seL4_Word *ret_svaddr, size_t *ret_buf_page_left) {
    assert(ret_svaddr != NULL);

    sync_acquire(ft_lock);
    /* Find relevant page table entry */
    struct pt_entry *pte = vaddr_to_pt_entry(proc->as, (seL4_Word)usr_buf);
    seL4_Word offset = ((seL4_Word)usr_buf - ((seL4_Word)usr_buf / PAGE_SIZE) * PAGE_SIZE);
    if (pte == NULL || pte->frame <= 0) {
        /* Mapping not valid, use pt_add_page to handle everything */
        int err = pt_add_page(proc, (seL4_Word)usr_buf, ret_svaddr, NULL);
        if (err) {
            sync_release(ft_lock);
            return err;
        }
    } else {
        /* Page exists, pin it */
        *ret_svaddr = (seL4_Word)pte->frame;
        frame_change_swappable(*ret_svaddr, 0);
    }
    *ret_svaddr += offset;
    
    if (ret_buf_page_left != NULL) {
        if (PAGE_SIZE - offset < buf_size) {
            *ret_buf_page_left = PAGE_SIZE - offset;   
        } else {
            *ret_buf_page_left = buf_size;
        }
    }

    sync_release(ft_lock);

    return 0;
}

/*
 * Does the heavy lifting of copyin/out
 * 
 * copyout should be true for copyout, false for copyin
 * is_string should be true to stop at terminating nulls
 */
static int docopy(process_t *proc, void *usr, void *sos, size_t nbytes, bool is_string, bool copyout) {
    bool region_r, region_w;
    /* Check that the entire user buffer lies within a valid region */
    if (!usr_buf_in_region(proc, usr, nbytes, &region_r, &region_w)) {
        dprintf(0, "No region - copyout\n");
        return EACCES;
    }
    if ((!region_r && !copyout) || (!region_w && copyout)) {
        dprintf(0, "No perms - copyout\n");
        return EACCES;
    }

    void *svaddr;
    /* Convert sos and usr addresses into dest and src based on copyout */
    void **dst = copyout ? &svaddr : &sos;
    void **src = copyout ? &sos : &svaddr;

    /* Copy intermediate and final pages */
    size_t to_copy;
    while (nbytes > 0) {
        sync_acquire(ft_lock);
        int err = usr_buf_to_sos(proc, usr, nbytes, (seL4_Word*)(&svaddr), &to_copy);
        if (err) {
            sync_release(ft_lock);
            return err;
        }
        if (is_string) {
            strncpy(*dst, *src, to_copy);

            /* Check for end of string char */
            if (*((char*)(*dst + to_copy - 1)) == 0) {
                sync_release(ft_lock);
                return 0;
            }
        } else {
            memcpy(*dst, *src, to_copy);
        }
        /* Unpin page when done */
        frame_change_swappable((seL4_Word)svaddr, true);
        sync_release(ft_lock);

        nbytes -= to_copy;
        usr += to_copy;
        sos += to_copy;
    }

    return 0;
}

int copyin(process_t *proc, void *dest, void *src, size_t nbytes) {
    return docopy(proc, src, dest, nbytes, false, false);
}

int copyinstring(process_t *proc, void *dest, void *src, size_t nbytes) {
    return docopy(proc, src, dest, nbytes, true, false);
}


int copyout(process_t *proc, void *dest, void *src, size_t nbytes) {
    return docopy(proc, dest, src, nbytes, false, true);
}

int copyoutstring(process_t *proc, void *dest, void *src, size_t nbytes) {
    return docopy(proc, dest, src, nbytes, true, true);
}


