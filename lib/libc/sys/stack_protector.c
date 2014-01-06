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
 */

#include "namespace.h"
#include <sys/param.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "un-namespace.h"

extern int __sys_sigaction(int, const struct sigaction *, struct sigaction *);

static void __guard_setup(void) __attribute__ ((constructor));
static void __fail(const char *);
void __stack_chk_fail(void);
void __chk_fail(void);
void __stack_chk_fail_local(void);

long __stack_chk_guard[8] = {0, 0, 0, 0, 0, 0, 0, 0};

/* For compatibility with propolice used by gcc34. */
void __stack_smash_handler(char func[], int damaged);
extern __typeof(__stack_chk_guard) __guard __attribute__ ((alias ("__stack_chk_guard")));

static void
__guard_setup(void)
{
    int fd;
    ssize_t size;

    if (__stack_chk_guard[0] != 0)
	return;
    if ((fd = _open ("/dev/urandom", 0)) >= 0) {
	size = _read (fd, (char*)&__stack_chk_guard, sizeof(__stack_chk_guard));
	_close (fd);
	if (size == sizeof(__stack_chk_guard))
	    return;
    }

    /*
     * If a random generator can't be used, the protector switches the
     * guard to the "terminator canary"
     */
    ((char*)__stack_chk_guard)[0] = 0;
    ((char*)__stack_chk_guard)[1] = 0;
    ((char*)__stack_chk_guard)[2] = '\n';
    ((char*)__stack_chk_guard)[3] = 255;
}

static void
__fail(const char *msg)
{
    struct sigaction sa;
    sigset_t mask;

    /* Immediately block all signal handlers from running code */
    sigfillset(&mask);
    sigdelset(&mask, SIGABRT);
    _sigprocmask(SIG_BLOCK, &mask, NULL);

    /* This may fail on a chroot jail... */
    syslog(LOG_CRIT, "%s", msg);

    bzero(&sa, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    __sys_sigaction(SIGABRT, &sa, NULL);

    kill(getpid(), SIGABRT);

    _exit(127);
}

void
__stack_smash_handler(char func[], int damaged  __unused)
{
    static char buf[128];

    snprintf(buf, sizeof(buf), "stack overflow in function %s", func);
    __fail(buf);
}

void
__stack_chk_fail(void)
{
    __fail("stack overflow detected; terminated");
}

void
__chk_fail(void)
{
    __fail("buffer overflow detected; terminated");
}

void
__stack_chk_fail_local(void)
{
    __stack_chk_fail();
}
