/*
 * Copyright (c) 2013 Larisa Grigore <larisagrigore@gmail.com>.
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
 *	This product includes software developed by Adam Glass and Charles
 *	Hannum.
 * 4. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYSV_SHM_H_
#define _SYSV_SHM_H_

#include <sys/ipc.h>

/* This flag is used to mark a semaphore group
 * as removed by another process.
 */
#define SEG_ALREADY_REMOVED	2

struct shm_data {
	int fd;		/* The file descriptor of the file used as
			   shared memory. */
	size_t size;
	int shmid;
	int type;	/* shm, sem, msg or undo;
			undo segments are used for semops with UNDO flag set. */
	void *internal;
	int used;	/* Number of thread that use this segment. */
	int removed;	/* The segment was mark for removal by a thread. */
	int access;	/* Used only for sems to avoid a segfault when try to
			access a semaphore wthout permission. */
};

int _shmget(key_t, size_t, int, int);
void shmchild(void);

int sysvipc_shmget (key_t, size_t, int);
int sysvipc_shmctl (int, int, struct shmid_ds *);
void *sysvipc_shmat  (int, const void *, int);
int sysvipc_shmdt  (const void *);

struct shm_data *get_shmdata(int, int, int);
int set_shmdata_access(int, int);

#endif /* !_SYSV_SHM_H_ */
