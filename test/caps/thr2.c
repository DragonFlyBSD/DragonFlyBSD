/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/test/caps/thr2.c,v 1.1 2003/12/07 04:26:41 dillon Exp $
 */
#include <stdio.h>
#include <libcaps/defs.h>

extern int mp_lock;

static void thread1(void *data);
static void thread2(void *data);

int
main(int ac, char **av)
{
    thread_t td;
    thread_t td1;
    thread_t td2;

    uthread_init();
    lwkt_create(thread1, NULL, &td1, NULL, 0, -1, "thread1");
    lwkt_create(thread2, NULL, &td2, NULL, 0, -1, "thread2");
    lwkt_switch();
    printf("Main Exiting\n");
    exit(1);
}

void
thread1(void *data)
{
    int i;
    for (i = 0; i < 1000000; ++i)
	lwkt_switch();
    printf("done1\n");
}

void
thread2(void *data)
{
    int i;
    for (i = 0; i < 1000000; ++i)
	lwkt_switch();
    printf("done2\n");
    exit(0);
}

