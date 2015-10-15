#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <serial/serial.h>
#include <stdlib.h>
#include <limits.h>
#include <sel4/sel4.h>
#include <sel4/types.h>
#include "file.h"
#include "proc.h"

/* Global serial object which is initialised in console_init */
struct serial *serial;

/* Initialise serial and console specific locks */
void console_init();

/*
 * Blocking read, only one process gets to read at a time (the rest wait
 * on a lock). Order of reading is a queue (due to how the lock works).
 *
 * dest is a user address
 *
 * Returns number of bytes written and -error otherwise
 */
int console_read(process_t *proc, struct file_t *fe, uint32_t offset, void *dest, size_t nbytes);

/*
 * Blocking write.
 *
 * src is a user address
 *
 * Returns number of bytes written and -error otherwise
 */
int console_write(process_t *proc, struct file_t *fe, uint32_t offset, void *src, size_t nbytes);

#endif /* _CONSOLE_H_ */
