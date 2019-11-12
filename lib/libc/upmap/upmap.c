/*
 * Copyright (c) 2014,2019 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "namespace.h"
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/upmap.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "un-namespace.h"
#include "libc_private.h"
#include "upmap.h"

/*
 * kpmap - Global user/kernel shared map (RO)
 * upmap - Per-process user/kernel shared map (RW)
 * lpmap - Per-thread user/kernel shared map (RW)
 */
static pthread_mutex_t ukpmap_lock;
static ukpheader_t *__kpmap_headers;
static ukpheader_t *__upmap_headers;
__thread ukpheader_t *__lpmap_headers TLS_ATTRIBUTE;
__thread uint32_t *__lpmap_blockallsigs TLS_ATTRIBUTE;

static __thread int lpmap_ok;
static int kpmap_ok;
static int upmap_ok;
static pthread_once_t upmap_once = PTHREAD_ONCE_INIT;


/*
 * Map the requested data item from the user-kernel global shared mmap
 *
 * *state is set to -1 on failure, else it is left alone.
 * *datap is set to a pointer to the item on success, else it is left alone.
 * If type == 0 this function finalizes state, setting it to 1 if it is 0.
 */
void
__kpmap_map(void *datap, int *state, uint16_t type)
{
	ukpheader_t *head;

	if (__isthreaded)
		_pthread_mutex_lock(&ukpmap_lock);

	if (kpmap_ok <= 0) {
		int fd;

		if (kpmap_ok < 0)
			goto failed;
		fd = _open("/dev/kpmap", O_RDONLY);
		if (fd < 0) {
			kpmap_ok = -1;
			goto failed;
		}
		__kpmap_headers = mmap(NULL, KPMAP_MAPSIZE,
				       PROT_READ, MAP_SHARED | MAP_FILE,
				       fd, 0);
		_close(fd);
		if ((void *)__kpmap_headers == MAP_FAILED) {
			kpmap_ok = -1;
			goto failed;
		}
		kpmap_ok = 1;
	}

	/*
	 * Special case to finalize state
	 */
	if (type == 0) {
		if (*state == 0)
			*state = 1;
		if (__isthreaded)
			_pthread_mutex_unlock(&ukpmap_lock);
		return;
	}

	/*
	 * Look for type.
	 */
	for (head = __kpmap_headers; head->type; ++head) {
		if (head->type == type) {
			*(void **)datap = (char *)__kpmap_headers +
					  head->offset;
			if (__isthreaded)
				_pthread_mutex_unlock(&ukpmap_lock);
			return;
		}
	}
failed:
	*state = -1;
	if (__isthreaded)
		_pthread_mutex_unlock(&ukpmap_lock);
}

/*
 * Map the requested data item from the user-kernel per-process shared mmap
 *
 * *state is set to -1 on failure, else it is left alone.
 * *datap is set to a pointer to the item on success, else it is left alone.
 * If type == 0 this function finalizes state, setting it to 1 if it is 0.
 */
void
__upmap_map(void *datap, int *state, uint16_t type)
{
	ukpheader_t *head;

	if (__isthreaded)
		_pthread_mutex_lock(&ukpmap_lock);

	if (upmap_ok <= 0) {
		int fd;

		if (upmap_ok < 0)
			goto failed;
		fd = _open("/dev/upmap", O_RDWR);
		if (fd < 0) {
			upmap_ok = -1;
			goto failed;
		}
		__upmap_headers = mmap(NULL, UPMAP_MAPSIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_SHARED | MAP_FILE,
				       fd, 0);
		_close(fd);
		if ((void *)__upmap_headers == MAP_FAILED) {
			upmap_ok = -1;
			goto failed;
		}
		upmap_ok = 1;
	}

	/*
	 * Special case to finalize state
	 */
	if (type == 0) {
		if (*state == 0)
			*state = 1;
		if (__isthreaded)
			_pthread_mutex_unlock(&ukpmap_lock);
		return;
	}

	/*
	 * Look for type.
	 */
	for (head = __upmap_headers; head->type; ++head) {
		if (head->type == type) {
			*(void **)datap = (char *)__upmap_headers +
					  head->offset;
			if (__isthreaded)
				_pthread_mutex_unlock(&ukpmap_lock);
			return;
		}
	}
failed:
	*state = -1;
	if (__isthreaded)
		_pthread_mutex_unlock(&ukpmap_lock);
}

/*
 * Map the requested data item from the user-kernel per-thread shared mmap
 *
 * *state is set to -1 on failure, else it is left alone.
 * *datap is set to a pointer to the item on success, else it is left alone.
 * If type == 0 this function finalizes state, setting it to 1 if it is 0.
 *
 * WARNING!  This code is used all over pthreads and must NOT make any
 *	     reentrant pthreads calls until after the mapping has been
 *	     set up.
 */
static pthread_key_t lpmap_key;

static void lpmap_unmap(void **datap);

void
__lpmap_map(void *datap, int *state, uint16_t type)
{
	ukpheader_t *head;

	if (lpmap_ok <= 0) {
		int fd;

		if (lpmap_ok < 0)
			goto failed;
		fd = _open("/dev/lpmap", O_RDWR);
		if (fd < 0) {
			lpmap_ok = -1;
			goto failed;
		}
		__lpmap_headers = mmap(NULL, LPMAP_MAPSIZE,
				       PROT_READ | PROT_WRITE,
				       MAP_SHARED | MAP_FILE,
				       fd, 0);
		_close(fd);
		if ((void *)__lpmap_headers == MAP_FAILED) {
			lpmap_ok = -1;
			goto failed;
		}
		lpmap_ok = 1;
		_pthread_setspecific(lpmap_key, &__lpmap_headers);
	}

	/*
	 * Special case to finalize state
	 */
	if (type == 0) {
		if (*state == 0)
			*state = 1;
		return;
	}

	/*
	 * Look for type.
	 */
	for (head = __lpmap_headers; head->type; ++head) {
		if (head->type == type) {
			*(void **)datap = (char *)__lpmap_headers +
					  head->offset;
			return;
		}
	}
failed:
	*state = -1;
}

/*
 * Cleanup thread state
 */
static void
lpmap_unmap(void **datap)
{
	ukpheader_t *lpmap = *datap;

	lpmap_ok = -1;
	if (lpmap) {
		__lpmap_blockallsigs = NULL;
		*datap = NULL;
		munmap(lpmap, LPMAP_MAPSIZE);
	}
}

/*
 * upmap initialization code, _upmap_thr_init() is called for the initial
 * main thread by libc or pthreads, and on every thread create.  We need
 * the __lpmap_blockallsigs pointer ASAP because it is used everywhere in
 * pthreads.
 *
 * If pthreads is not linked in, _pthread_once() still runs via a stub in
 * libc, and _pthread_key_create() is a NOP.
 *
 * NOTE: These pthreads calls are stubs when pthreads is not linked in.
 *	 The once routine will still be run once regardless.
 */
static
void
_upmap_init_once(void)
{
	_pthread_key_create(&lpmap_key, (void (*)(void *))lpmap_unmap);
}

void
_upmap_thr_init(void)
{
	int dummy_state = 0;

        _pthread_once(&upmap_once, _upmap_init_once);
	__lpmap_map(&__lpmap_blockallsigs, &dummy_state, LPTYPE_BLOCKALLSIGS);
}
