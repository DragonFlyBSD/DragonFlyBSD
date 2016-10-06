/*-
 * Copyright (c) 2006 David Xu <davidxu@freebsd.org>
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
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/sysproto.h>
#include <sys/mqueue.h>

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <signal.h>

extern int      __sys_mq_open(const char *, int, mode_t,
    const struct mq_attr *);
extern int      __sys_mq_close(int fd);
extern int	__sys_mq_getattr(mqd_t mqd, struct mq_attr *attr);
extern int	__sys_mq_notify(int, const struct sigevent *);
extern ssize_t	__sys_mq_receive(mqd_t mqd, char *buf, size_t len,
    unsigned *prio);
extern ssize_t	__sys_mq_send(mqd_t mqd, char *buf, size_t len, unsigned prio);
extern int	__sys_mq_setattr(int, const struct mq_attr *__restrict,
    struct mq_attr *__restrict);
extern ssize_t	__sys_mq_timedreceive(int, char *__restrict, size_t,
    unsigned *__restrict, const struct timespec *__restrict);
extern int	__sys_mq_timedsend(int, const char *, size_t, unsigned,
    const struct timespec *);
extern int	__sys_mq_unlink(const char *);

mqd_t
__mq_open(const char *name, int oflag, mode_t mode,
	const struct mq_attr *attr)
{
	return (__sys_mq_open(name, oflag, mode, attr));
}

int
__mq_close(mqd_t mqd)
{
	return (__sys_mq_close(mqd));
}

int
__mq_notify(mqd_t mqd, const struct sigevent *evp)
{
	return (__sys_mq_notify(mqd, evp));
}

int
__mq_getattr(mqd_t mqd, struct mq_attr *attr)
{
	return (__sys_mq_getattr(mqd, attr));
}

int
__mq_setattr(mqd_t mqd, const struct mq_attr *newattr, struct mq_attr *oldattr)
{
	return (__sys_mq_setattr(mqd, newattr, oldattr));
}

ssize_t
__mq_timedreceive(mqd_t mqd, char *buf, size_t len,
	unsigned *prio, const struct timespec *timeout)
{
	return (__sys_mq_timedreceive(mqd, buf, len, prio, timeout));
}

ssize_t
__mq_receive(mqd_t mqd, char *buf, size_t len, unsigned *prio)
{
	return (__sys_mq_receive(mqd, buf, len, prio));
}

ssize_t
__mq_timedsend(mqd_t mqd, char *buf, size_t len,
	unsigned prio, const struct timespec *timeout)
{
	return (__sys_mq_timedsend(mqd, buf, len, prio, timeout));
}

ssize_t
__mq_send(mqd_t mqd, char *buf, size_t len, unsigned prio)
{
	return (__sys_mq_send(mqd, buf, len, prio));
}

int
__mq_unlink(const char *path)
{
	return (__sys_mq_unlink(path));
}

__weak_reference(__mq_open, mq_open);
__weak_reference(__mq_open, _mq_open);
__weak_reference(__mq_close, mq_close);
__weak_reference(__mq_close, _mq_close);
__weak_reference(__mq_notify, mq_notify);
__weak_reference(__mq_notify, _mq_notify);
__weak_reference(__mq_getattr, mq_getattr);
__weak_reference(__mq_getattr, _mq_getattr);
__weak_reference(__mq_setattr, mq_setattr);
__weak_reference(__mq_setattr, _mq_setattr);
__weak_reference(__mq_timedreceive, mq_timedreceive);
__weak_reference(__mq_timedreceive, _mq_timedreceive);
__weak_reference(__mq_timedsend, mq_timedsend);
__weak_reference(__mq_timedsend, _mq_timedsend);
__weak_reference(__mq_unlink, mq_unlink);
__weak_reference(__mq_unlink, _mq_unlink);
__weak_reference(__mq_send, mq_send);
__weak_reference(__mq_send, _mq_send);
__weak_reference(__mq_receive, mq_receive);
__weak_reference(__mq_receive, _mq_receive);
