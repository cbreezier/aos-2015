/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 test.
 *
 *      Author:			Godfrey van der Linden
 *      Original Author:	Ben Leslie
 *
 ****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <sel4/sel4.h>


#include "ttyout.h"


#define NPAGES 27

/* called from pt_test */
static void
do_pt_test( char *buf )
{
    int i;

    /* set */
    for(i = 0; i < NPAGES; i ++)
	buf[i * 4096] = i;

    /* check */
    for(i = 0; i < NPAGES; i ++)
	assert(buf[i * 4096] == i);
}

static void
pt_test( void )
{
    /* need a decent sized stack */
    char buf1[NPAGES * 4096], *buf2 = NULL;

    /* check the stack is above phys mem */
    assert((void *) buf1 > (void *) 0x20000000);

    /* stack test */
    do_pt_test(buf1);
    printf("Stack test ok\n");

    /* heap test */
    buf2 = malloc(1500000);
    printf("%p\n", buf2);
    assert(buf2);
    do_pt_test(buf2);
    free(buf2);

    buf2 = malloc(1500000);
    printf("%p\n", buf2);
    assert(buf2);
    do_pt_test(buf2);

    char *buf3 = malloc(1500000);
    do_pt_test(buf3);
    printf("%p\n", buf3);
    free(buf3);
    free(buf2);
    printf("Heap test ok\n");

}

// Block a thread forever
// we do this by making an unimplemented system call.
static void
thread_block(void){
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetTag(tag);
    seL4_SetMR(0, 1);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
}

int main(void){
    pt_test();
    printf("done pt test\n");

    /* initialise communication */
    ttyout_init();

    do {
        printf("task:\tHello world, I'm\ttty_test!\n");
        thread_block();
        // sleep(1);	// Implement this as a syscall
    } while(1);

    return 0;
}
