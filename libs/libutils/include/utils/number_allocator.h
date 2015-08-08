
#ifndef _NUMBER_ALLOCATOR_H_
#define _NUMBER_ALLOCATOR_H_

#include <sel4/sel4.h>

struct allocation;
struct number_allocator {
    /* BST root keeping track of all allocated numbers */
    struct allocation *root;

    /* Represents the state of the PRNG */
    uint32_t seed;
};

/*
 * Creates an empty number_allocator
 */
struct number_allocator *init_allocator(uint32_t seed);

/*
 * Returns a valid 32bit unsigned int from the
 * supplied number allocator
 */
uint32_t allocator_get_num(struct number_allocator *na);

/*
 * Releases a number from the allocator
 */
void allocator_release_num(struct number_allocator *na, uint32_t num);

/*
 * Destroys the number allocator and frees
 * all its memory
 */
void destroy_allocator(struct number_allocator *na);

#endif /* _NUMBER_ALLOCATOR_H_ */
