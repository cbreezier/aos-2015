
#ifndef _NUMBER_ALLOCATOR_H_
#define _NUMBER_ALLOCATOR_H_

#include <clock/clock.h>

struct allocation;
struct number_allocator {
    struct allocation *root;
    uint32_t seed;
};

/*
 * Creates an empty number_allocator
 */
struct number_allocator *init_allocator(void);

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

bool allocator_assert_num(struct number_allocator *na, uint32_t num);

#endif /* _NUMBER_ALLOCATOR_H_ */
