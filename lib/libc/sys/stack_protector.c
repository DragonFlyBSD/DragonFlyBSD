/*
 * Copyright (c) 2002 Hiroaki Etoh, Federico G. Schwindt, and Miodrag Vallat.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $OpenBSD: stack_protector.c,v 1.3 2002/12/10 08:53:42 etoh Exp $
 * $DragonFly: src/lib/libc/sys/stack_protector.c,v 1.1 2003/12/10 22:15:36 dillon Exp $
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <syslog.h>

void __stack_smash_handler(char func[], int damaged __attribute__((unused)));
static void __guard_setup(void) __attribute__ ((constructor));

long __guard[8] = {0, 0, 0, 0, 0, 0, 0, 0};

static void
__guard_setup(void)
{
    int fd;
    ssize_t size;

    if (__guard[0] != 0)
	return;
    if ((fd = open ("/dev/urandom", 0)) >= 0) {
	size = read (fd, (char*)&__guard, sizeof(__guard));
	close (fd);
	if (size == sizeof(__guard))
	    return;
    }

    /*
     * If a random generator can't be used, the protector switches the
     * guard to the "terminator canary"
     */
    ((char*)__guard)[0] = 0;
    ((char*)__guard)[1] = 0;
    ((char*)__guard)[2] = '\n';
    ((char*)__guard)[3] = 255;
}

void
__stack_smash_handler(char func[], int damaged)
{
    const char message[] = "stack overflow in function %s";
    struct sigaction sa;

    /* this may fail on a chroot jail, though luck */
    syslog(LOG_CRIT, message, func);

    bzero(&sa, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    sigaction(SIGABRT, &sa, NULL);

    kill(getpid(), SIGABRT);

    _exit(127);
}
