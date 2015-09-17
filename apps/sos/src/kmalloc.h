#ifndef _KMALLOC_H_
#define _KMALLOC_H_

void kmalloc_init();

void *kmalloc(size_t n);

void kfree(void *buf);

#endif /* _KMALLOC_H_ */
