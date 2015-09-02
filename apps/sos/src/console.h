#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <serial/serial.h>
#include <stdlib.h>
#include <limits.h>
#include <sel4/sel4.h>
#include <sel4/types.h>
#include "file.h"

struct serial *serial;

void console_init();

int console_read(struct file_t *file, uint32_t offset, void *dest, size_t nbytes);

int console_write(struct file_t *file, uint32_t offset, void *src, size_t nbytes);

#endif /* _CONSOLE_H_ */
