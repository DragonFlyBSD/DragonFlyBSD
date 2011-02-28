/*-
 * Copyright (c) 2011 Venkatesh Srinivas <vsrinivas@dragonflybsd.org>  
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 */

/*
 * This file contains support for the POSIX 1003.1B AIO/LIO facility.
 *
 * This version of AIO is aimed at standards compliance; it is not aimed
 * at either reasonability or performance. It merely wraps synchronous I/O
 * routines.
 *
 * This version cannot perform asynchronous notification of I/O completion
 * on DragonFly via SIGEV_SIGNAL.
 *
 *    2) SIGEV_SIGNAL code is present, but if-ed out under 'notyet'; Dfly
 *	 does not support sigqueue(), so we cannot control the payload to
 *	 a SIGINFO-signal from userspace. Programs using AIO signals use
 *	 the payload field to carry pointers to the completed AIO structure,
 *	 which is not yet possible here.
 *
 * It would be possible for this version to support SIGEV_KEVENT.
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <sys/queue.h>
#include <unistd.h>
#include <errno.h>
#include <sys/aio.h>
#include <pthread.h>

static void 	*_aio_thr_start(void *);

static void
_aio_notify(struct sigevent *sigevp)
{
#if 0 /* not yet */
	int sig;
	union sigval sv;
	pid_t pid;
#endif
	pthread_t thr;

	switch (sigevp->sigev_notify) {
	case SIGEV_NONE:
		return;

	case SIGEV_THREAD:
		pthread_create(&thr, 
			       sigevp->sigev_notify_attributes,
			       _aio_thr_start,
			       sigevp);
		break;

	case SIGEV_SIGNAL:
#if 0 /* not yet */
		pid = getpid();
		sig = sigevp->sigev_signo;
		sv = sigevp->sigev_value;
		sigqueue(pid, sig, sv);
#endif
		break;

	case SIGEV_KEVENT:
		/* not yet */
		break;
	}
}

static void *
_aio_thr_start(void *vsigevp)
{
	struct sigevent *sigevp = vsigevp;
	sigevp->sigev_notify_function(sigevp->sigev_value);
	return (NULL);
}

/*
 * aio_read()
 *
 *	Asynchronously read from a file
 */
int 
aio_read(struct aiocb *ap)
{
#ifndef notyet
	if ((ap->aio_sigevent.sigev_notify != SIGEV_NONE) &&
	    (ap->aio_sigevent.sigev_notify != SIGEV_THREAD))
		return (ENOSYS);
#endif

	ap->_aio_val = pread(ap->aio_fildes, 
			     (void *) ap->aio_buf, 
			     ap->aio_nbytes,
			     ap->aio_offset);
	ap->_aio_err = errno;

	_aio_notify(&ap->aio_sigevent);

	return (0);
}

int 
aio_write(struct aiocb *ap)
{
#ifndef notyet
	if ((ap->aio_sigevent.sigev_notify != SIGEV_NONE) &&
	    (ap->aio_sigevent.sigev_notify != SIGEV_THREAD))
		return (ENOSYS);
#endif

	ap->_aio_val = pwrite(ap->aio_fildes,
			      (void *) ap->aio_buf, 
			      ap->aio_nbytes,
			      ap->aio_offset);
	ap->_aio_err = errno;

	_aio_notify(&ap->aio_sigevent);

	return (0);
}

int
aio_fsync(int op, struct aiocb *ap)
{
#ifndef notyet
	if ((ap->aio_sigevent.sigev_notify != SIGEV_NONE) &&
	    (ap->aio_sigevent.sigev_notify != SIGEV_THREAD))
		return (ENOSYS);
#endif

	ap->_aio_val = fsync(ap->aio_fildes);
	ap->_aio_err = errno;

	_aio_notify(&ap->aio_sigevent);

	return(0);
}

int 
lio_listio(int mode, struct aiocb *const apv[], int nent, 
	   struct sigevent *sigevp)
{
	int i;

#ifndef notyet
	if ((sigevp->sigev_notify != SIGEV_NONE) &&
	    (sigevp->sigev_notify != SIGEV_THREAD))
		return (ENOSYS);
#endif

	for (i = 0; i < nent; i++)
		switch (apv[i]->aio_lio_opcode) {
		case LIO_READ:
			aio_read(apv[i]);
			break;
		case LIO_WRITE:
			aio_write(apv[i]);
			break;
		case LIO_NOP:
			break;
		}

	if (sigevp && 
	    (mode == LIO_NOWAIT)
	   ) {
		_aio_notify(sigevp);
	}

	return (0);
}

/*
 * aio_error()
 *
 * 	Get I/O completion status
 *
 *      Returns EINPROGRESS until I/O is complete. Returns ECANCELED if
 *	I/O is canceled. Returns I/O status if operation completed.
 *
 *      This routine does not block.
 */
int 
aio_error(const struct aiocb *ap)
{
	return (ap->_aio_err);
}

/*
 * aio_return()
 *
 *	Finish up I/O, releasing I/O resources and returns the value
 *	that would have been associated with a synchronous request.
 */
ssize_t
aio_return(struct aiocb *ap)
{
	return (ap->_aio_val);
}

int
aio_cancel(int fildes, struct aiocb *aiocbp)
{
	return (AIO_ALLDONE);
}

int
aio_suspend(const struct aiocb *const list[], int nent, const struct timespec *timo)
{
	return (0);
}

