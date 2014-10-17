/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
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

static pthread_mutex_t ukpmap_lock;
static ukpheader_t *kpmap_headers;
static ukpheader_t *upmap_headers;
static int kpmap_ok;
static int upmap_ok;

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
		kpmap_headers = mmap(NULL, KPMAP_MAPSIZE,
				     PROT_READ, MAP_SHARED,
				     fd, 0);
		_close(fd);
		if ((void *)kpmap_headers == MAP_FAILED) {
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
	for (head = kpmap_headers; head->type; ++head) {
		if (head->type == type) {
			*(void **)datap = (char *)kpmap_headers + head->offset;
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
		upmap_headers = mmap(NULL, UPMAP_MAPSIZE,
				     PROT_READ | PROT_WRITE, MAP_SHARED,
				     fd, 0);
		_close(fd);
		if ((void *)upmap_headers == MAP_FAILED) {
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
	for (head = upmap_headers; head->type; ++head) {
		if (head->type == type) {
			*(void **)datap = (char *)upmap_headers + head->offset;
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
