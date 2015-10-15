/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#ifndef _LIBOS_ELF_H_
#define _LIBOS_ELF_H_

#include <sel4/sel4.h>
#include <addrspace.h>

/*
 * Loads elf file via nfs given a file name
 * ret_entrypoint will contain the entrypoint of the loaded program
 *
 * Returns 0 on success and error code otherwise
 */
int elf_load(process_t *proc, char* file_name, seL4_Word *ret_entrypoint);

#endif /* _LIBOS_ELF_H_ */
