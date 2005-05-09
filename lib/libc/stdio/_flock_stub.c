/*
 * Copyright (c) 1998 John Birrell <jb@cimlogic.com.au>.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Birrell.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/libc/stdio/_flock_stub.c,v 1.3 1999/08/28 00:00:55 peter Exp $
 * $DragonFly: src/lib/libc/stdio/_flock_stub.c,v 1.7 2005/05/09 12:43:40 davidxu Exp $
 *
 */

#include "namespace.h"
#include <stdio.h>
#include <pthread.h>
#include "un-namespace.h"

#include "local.h"

/* Don't build this in libc_r, just libc: */
/*
 * Declare weak references in case the application is not linked
 * with libpthread.
 */
__weak_reference(_flockfile_stub,flockfile);
__weak_reference(_flockfile_stub,_flockfile);
__weak_reference(_flockfile_debug_stub,_flockfile_debug);
__weak_reference(_ftrylockfile_stub,ftrylockfile);
__weak_reference(_ftrylockfile_stub,_ftrylockfile);
__weak_reference(_funlockfile_stub,funlockfile);
__weak_reference(_funlockfile_stub,_funlockfile);

void	flockfile(FILE *);
void	_flockfile_debug(FILE *, char *, int);
int 	ftrylockfile(FILE *);
void	funlockfile(FILE *);

#define _lock _extra

/*
 * This function is a stub for the _flockfile function in libpthread.
 */
void
_flockfile_stub(FILE *fp)
{
	pthread_t curthread = _pthread_self();

	if (fp->_lock->fl_owner == curthread)
		fp->_lock->fl_count++;
	else {
		/*
		 * Make sure this mutex is treated as a private
		 * internal mutex:
		 */
		_pthread_mutex_lock(&fp->_lock->fl_mutex);
		fp->_lock->fl_owner = curthread;
		fp->_lock->fl_count = 1;
	}
}

/*
 * This function is a stub for the _flockfile_debug function in libpthread.
 */
void
_flockfile_debug_stub(FILE *fp, char *fname, int lineno)
{
	_flockfile(fp);
}

/*
 * This function is a stub for the _ftrylockfile function in libpthread.
 */
int
_ftrylockfile_stub(FILE *fp)
{
	pthread_t curthread = _pthread_self();
	int	ret = 0;

	if (fp->_lock->fl_owner == curthread)
		fp->_lock->fl_count++;
	/*
	 * Make sure this mutex is treated as a private
	 * internal mutex:
	 */
	else if (_pthread_mutex_trylock(&fp->_lock->fl_mutex) == 0) {
		fp->_lock->fl_owner = curthread;
		fp->_lock->fl_count = 1;
	}
	else
		ret = -1;
	return (ret);
}

/*
 * This function is a stub for the _funlockfile function in libpthread.
 */
void
_funlockfile_stub(FILE *fp)
{
	pthread_t	curthread = _pthread_self();

	/*
	 * Check if this file is owned by the current thread:
	 */
	if (fp->_lock->fl_owner == curthread) {
		/*
		 * Check if this thread has locked the FILE
		 * more than once:
		 */
		if (fp->_lock->fl_count > 1)
			/*
			 * Decrement the count of the number of
			 * times the running thread has locked this
			 * file:
			 */
			fp->_lock->fl_count--;
		else {
			/*
			 * The running thread will release the
			 * lock now:
			 */
			fp->_lock->fl_count = 0;
			fp->_lock->fl_owner = NULL;
			_pthread_mutex_unlock(&fp->_lock->fl_mutex);
		}
	}
}
