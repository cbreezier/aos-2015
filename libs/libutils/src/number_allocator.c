#include <utils/number_allocator.h>
#include <stdlib.h>

#define DEFAULT_SEED 1000000009

/* Represents the allocation of one number as a node in a BST */
struct allocation {
    uint32_t num;
    struct allocation *left;
    struct allocation *right;
};

/* Returns the new root of the subtree after insertion */
static struct allocation *insert_num(struct allocation *node, uint32_t num, bool *success) {
    if (node == NULL) {
        node = malloc(sizeof(struct allocation));
        if (node == NULL) {
            *success = false;
            return NULL;
        }
        node->num = num;
        node->left = NULL;
        node->right = NULL;
        *success = true;
    } else if (num == node->num) {
        *success = false;
    } else if (num > node->num) {
        node->right = insert_num(node->right, num, success);
    } else if (num < node->num) {
        node->left = insert_num(node->left, num, success);
    } else {
        *success = false;
        node = NULL;
    }
    
    return node;
}

/* Returns the minimum value within a subtree */
static uint32_t tree_min(struct allocation *node) {
    if (node == NULL) {
        return -1;
    }
    return (node->left == NULL) ? node->num : tree_min(node->left);
}

/* Returns the new root of the subtree after removal */
static struct allocation *remove_num(struct allocation *node, uint32_t num) {
    if (node == NULL) {
        return NULL;
    }
    
    struct allocation *to_return = NULL;
    if (num == node->num) {
        if (node->left == NULL && node->right == NULL) {
            free(node);
            return NULL;
        }
        if (node->left != NULL && node->right != NULL) {
            uint32_t smallest = tree_min(node->right);
            if (smallest == -1) return node;
            node->right = remove_num(node->right, smallest);
            node->num = smallest;
            return node;
        }

        if (node->left != NULL) {
            to_return = node->left;
        } else if (node->right != NULL) {
            to_return = node->right;
        }

        free(node);
    } else if (num > node->num) {
        node->right = remove_num(node->right, num);
        to_return = node;
    } else if (num < node->num) {
        node->left = remove_num(node->left, num);
        to_return = node;
    }

    return to_return;
}

static void destroy_allocations(struct allocation *node) {
    if (node == NULL) return;

    destroy_allocations(node->left);
    destroy_allocations(node->right);
    free(node);
}

/* XORShift PRNG */
static uint32_t gen_random(struct number_allocator *na) {
    na->seed ^= (na->seed << 13);
    na->seed ^= (na->seed >> 17);
    na->seed ^= (na->seed <<  5);

    return na->seed*1597334677;
}

struct number_allocator *init_allocator(uint32_t seed) {
    struct number_allocator *na = malloc(sizeof(struct number_allocator));
    if (na == NULL) {
        return NULL;
    }
    na->root = NULL;

    /* seed for random number generation */
    if (seed == 0) {
        seed = DEFAULT_SEED;
    }
    na->seed = seed;

    return na;
}

/* 
 * Randomly generate numbers until one is found which is not
 * currently allocated.
 */
uint32_t allocator_get_num(struct number_allocator *na) {
    uint32_t num;
    bool success = false;
    do {
        do {
            num = gen_random(na);
        } while (num == 0);
        na->root = insert_num(na->root, num, &success);
    } while (!success);

    return num;
}

void allocator_release_num(struct number_allocator *na, uint32_t num) {
    na->root = remove_num(na->root, num);
}

void destroy_allocator(struct number_allocator *na) {
    destroy_allocations(na->root);
    free(na);
}
