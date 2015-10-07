/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <sel4/sel4.h>
#include <elf/elf.h>
#include <string.h>
#include <assert.h>
#include <cspace/cspace.h>
#include <elf/elf32.h>

#include "elf.h"
#include "pagetable.h"
#include "frametable.h"
#include "vmem_layout.h"
#include "ut_manager/ut.h"
#include "alloc_wrappers.h"
#include "file.h"
#include "nfs_sync.h"

#include <utils/mapping.h>

#include <sys/debug.h>
#include <sys/panic.h>

/* Minimum of two values. */
#define MIN(a,b) (((a)<(b))?(a):(b))

#ifndef PAGESIZE
    #define PAGESIZE              (1 << (seL4_PageBits))
#endif
#define PAGEMASK              ((PAGESIZE) - 1)
#define PAGE_ALIGN(addr)      ((addr) & ~(PAGEMASK))
#define IS_PAGESIZE_ALIGNED(addr) !((addr) &  (PAGEMASK))


/*
 * Convert ELF permissions into seL4 permissions.
 */
static inline seL4_Word get_sel4_rights_from_elf(unsigned long permissions) {
    seL4_Word result = 0;

    if (permissions & PF_R)
        result |= seL4_CanRead;
    if (permissions & PF_X)
        result |= seL4_CanRead;
    if (permissions & PF_W)
        result |= seL4_CanWrite;

    return result;
}

/*
 * Inject data into the given vspace.
 * TODO: Don't keep these pages mapped in
 */
static int load_segment_into_vspace(process_t *proc,
                                    unsigned long offset, unsigned long segment_size,
                                    unsigned long file_size, unsigned long dst,
                                    unsigned long permissions, fhandle_t *fhandle) {

    /* Overview of ELF segment loading

       dst: destination base virtual address of the segment being loaded
       segment_size: obvious
       
       So the segment range to "load" is [dst, dst + segment_size).

       The content to load is either zeros or the content of the ELF
       file itself, or both.

       The split between file content and zeros is a follows.

       File content: [dst, dst + file_size)
       Zeros:        [dst + file_size, dst + segment_size)

       Note: if file_size == segment_size, there is no zero-filled region.
       Note: if file_size == 0, the whole segment is just zero filled.

       The code below relies on seL4's frame allocator already
       zero-filling a newly allocated frame.

    */

    dprintf(0, "Loading segment into %lu of size %lu\n", dst, segment_size);


    assert(file_size <= segment_size);


    int err = as_add_region(proc->as, dst, segment_size,
        permissions & seL4_CanRead,
        permissions & seL4_CanWrite,
        permissions & seL4_CanRead);

    if (err) {
        return err;
    }

    err = nfs_read_sync(proc, fhandle, offset, (void*)dst, file_size);
    if (err) {
        return err;
    }

    return 0;
    //dprintf(0, "region added\n");

    /* We work a page at a time in the destination vspace. */
//    unsigned long pos = 0;
//    while(pos < segment_size) {
//        int nbytes;
//        seL4_Word vaddr = PAGE_ALIGN(dst);
//
//        /* Now copy our data into the destination vspace. */
//        seL4_Word svaddr;
//        seL4_CPtr sos_cap;
//        err = pt_add_page(proc, vaddr, &svaddr, &sos_cap);
//        if (err) {
//            //dprintf(0, "error is %u\n", err);
//            return err;
//        }
//        nbytes = PAGESIZE - (dst & PAGEMASK);
//        if (pos < file_size){
//            memcpy((void*)svaddr + (PAGE_SIZE - nbytes), (void*)src, MIN(nbytes, file_size - pos));
//        }
//        frame_change_swappable(svaddr, 1);
//
//        /* Not observable to I-cache yet so flush the frame */
//        seL4_ARM_Page_Unify_Instruction(sos_cap, 0, PAGESIZE);
//
//        pos += nbytes;
//        dst += nbytes;
//        src += nbytes;
//    }
//    return 0;
}

int elf_load(process_t *proc, char *file_name, seL4_Word *ret_entrypoint) {

    int num_headers;
    int err;
    int i;

    fhandle_t fhandle;
    err = nfs_lookup_sync(file_name, &fhandle, NULL);
    if (err) {
        return err;
    }

    struct Elf32_Header header;

    err = nfs_sos_read_sync(fhandle, 0, (void*)(&header), sizeof(header));
    if (err) {
        return err;
    }

    void *elf_header = (void *)&header;

    /* Ensure that the ELF file looks sane. */
    if (elf_checkFile(elf_header)) {
        return seL4_InvalidArgument;
    }

    num_headers = elf_getNumProgramHeaders(elf_header);
    for (i = 0; i < num_headers; i++) {
        //char *source_addr, ;
        unsigned long flags, file_size, segment_size, vaddr, source_offset;

        /* Skip non-loadable segments (such as debugging data). */
        if (elf_getProgramHeaderType(elf_header, i) != PT_LOAD)
            continue;

        /* Fetch information about this segment. */
        source_offset = elf_getProgramHeaderOffset(elf_header, i);
        //source_addr = elf_header + source_offset;
        file_size = elf_getProgramHeaderFileSize(elf_header, i);
        segment_size = elf_getProgramHeaderMemorySize(elf_header, i);
        vaddr = elf_getProgramHeaderVaddr(elf_header, i);
        flags = elf_getProgramHeaderFlags(elf_header, i);

        /* Copy it across into the vspace. */
        dprintf(1, " * Loading segment %08x-->%08x\n", (int)vaddr, (int)(vaddr + segment_size));
        err = load_segment_into_vspace(proc, source_offset, segment_size, file_size, vaddr,
                                       get_sel4_rights_from_elf(flags) & seL4_AllRights, &fhandle);
        if (err) {
            return err;
        }
    }

    if (ret_entrypoint) {
        *ret_entrypoint = elf_getEntryPoint(elf_header);
    }

    as_unify_cache(proc->as);

    return 0;
}
