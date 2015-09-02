#include <copy.h>
#include <pagetable.h>
#include <string.h>

static inline int min(int a, int b) {
    return a < b ? a : b;
}

static int docopy(process_t *proc, void *usr, void *sos, size_t nbytes, bool is_string, bool copyout) {
    /* Check that the entire user buffer lies within a valid region */
    struct region_entry *path_region = as_get_region(proc->as, usr);
    if (path_region == NULL) {
        return EFAULT;
    }
    seL4_Word region_end = path_region->start + path_region->size;
    if (sizeof(seL4_Word)*(region_end - (seL4_Word)usr) <  nbytes) {
        return EFAULT;
    }

    void *svaddr;
    void **dst = copyout ? &svaddr : &sos;
    void **src = copyout ? &sos : &svaddr;
    /* 
     * Get the svaddr for the initial value of the user buffer, and copy until
     * a page aligned value. 
     */
    struct pt_entry *pte = vaddr_to_pt_entry(proc->as, (seL4_Word)usr);
    seL4_Word offset = ((seL4_Word)usr - ((seL4_Word)usr / PAGE_SIZE) * PAGE_SIZE);
    if (pte == NULL || pte->frame == 0) {
        int err = pt_add_page(proc, (seL4_Word)usr, (seL4_Word*)&svaddr, NULL, seL4_AllRights);
        if (err) {
            return err;
        }
    } else {
        svaddr = (void*)pte->frame;
    }
    svaddr += offset;
    /* Copy first page */
    size_t to_copy = min(nbytes, PAGE_SIZE - offset);
    if (is_string) {
        strncpy(*dst, *src, to_copy);
        /* Check for end of string char */
        if (*((char *)(*dst + to_copy - 1)) == 0) {
            return 0;
        }
    } else {
        memcpy(*dst, *src, to_copy);
    }

    nbytes -= to_copy;
    usr += to_copy;
    sos += to_copy;
    /* Copy intermediate and final pages */
    while (nbytes > 0) {
        pte = vaddr_to_pt_entry(proc->as, (seL4_Word)usr);
        if (pte == NULL || pte->frame == 0) {
            int err = pt_add_page(proc, (seL4_Word)usr, (seL4_Word*)&svaddr, NULL, seL4_AllRights);
            if (err) {
                return err;
            }
        } else {
            svaddr = (void*)pte->frame;
        }
        to_copy = min(nbytes, PAGE_SIZE);
        if (is_string) {
            strncpy(*dst, *src, to_copy);
            /* Check for end of string char */
            if (*((char*)(*dst + to_copy - 1)) == 0) {
                return 0;
            }
        } else {
            memcpy(*dst, *src, to_copy);
        }

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


