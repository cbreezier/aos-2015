#include <copy.h>
#include <pagetable.h>

static int docopyin(sos_process_t *proc, void *dest, void *src, size_t nbytes, bool is_string) {
    struct pt_entry *pte = vaddr_to_pt_entry(proc->as, (seL4_Word)src);
    seL4_Word svaddr = ((seL4_Word)src - ((seL4_Word)src / PAGE_SIZE) * PAGE_SIZE) + pte->frame;
    for (int i = 0; i < nbytes; ++svaddr, ++i, ++src) {
        if (svaddr % PAGE_SIZE == 0) {
            pte = vaddr_to_pt_entry(proc->as, (seL4_Word)src);
            if (pte == NULL || pte->frame == 0) {
                /* TODO M6: check if page is swapped out, don't assert - simply EFAULT */
                assert(!"Page not mapped - sos open");
                //err = EFAULT;
                //goto end;
            } else {
                svaddr = ((seL4_Word)src - ((seL4_Word)src / PAGE_SIZE) * PAGE_SIZE) + pte->frame;
            }
        }
        ((char*)dest)[i] = *((char*)svaddr);
        if (is_string && ((char*)dest)[i] == '\0') {
            break;
        }
    }
    return 0;
}

int copyin(sos_process_t *proc, void *dest, void *src, size_t nbytes) {
    return docopyin(proc, dest, src, nbytes, false);
}

int copyinstring(sos_process_t *proc, void *dest, void *src, size_t nbytes) {
    return docopyin(proc, dest, src, nbytes, true);
}

static int docopyout(sos_process_t *proc, void *dest, void *src, size_t nbytes, bool is_string) {
    struct region_entry *path_region = as_get_region(proc->as, dest);
    if (path_region == NULL) {
        return EFAULT;
    }

    seL4_Word region_end = path_region->start + path_region->size;
    if (sizeof(seL4_Word)*(region_end - (seL4_Word)dest) <  nbytes) {
        return EFAULT;
    }

    struct pt_entry *pte = vaddr_to_pt_entry(proc->as, (seL4_Word)dest);
    seL4_Word svaddr = ((seL4_Word)dest - ((seL4_Word)dest / PAGE_SIZE) * PAGE_SIZE) + pte->frame;
    for (int i = 0; i < nbytes; ++svaddr, ++i, ++dest) {
        if ((uint32_t)dest % PAGE_SIZE == 0) {
            pte = vaddr_to_pt_entry(proc->as, (seL4_Word)dest);
            if (pte == NULL || pte->frame == 0) {
                int err = pt_add_page(proc, (seL4_Word)dest, &svaddr, NULL, seL4_AllRights);
                if (err) {
                    return err;
                }
            } else {
                svaddr = ((seL4_Word)dest - ((seL4_Word)dest / PAGE_SIZE) * PAGE_SIZE) + pte->frame;
            }
        }
        *((char*)svaddr) = ((char*)src)[i];
        if (((char*)src)[i] == '\0' && is_string) {
            break;
        }
    }
    return 0;
}

int copyout(sos_process_t *proc, void *dest, void *src, size_t nbytes) {
    return docopyout(proc, dest, src, nbytes, false);
}

int copyoutstring(sos_process_t *proc, void *dest, void *src, size_t nbytes) {
    return docopyout(proc, dest, src, nbytes, true);
}

