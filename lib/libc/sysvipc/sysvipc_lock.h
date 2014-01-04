/**
 * Copyright (c) 2013 Larisa Grigore<larisagrigore@gmail.com>.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYSV_DFLY_UMTX_GEN_H_
#define _SYSV_DFLY_UMTX_GEN_H_

#include "sysvipc_lock_generic.h"

#define SYSV_RWLOCK

struct sysv_mutex {
	umtx_t _mutex_static_lock;
	int pid_owner;
	int tid_owner;
};

struct sysv_rwlock {
	struct sysv_mutex	lock;	/* monitor lock */
	int	read_signal;
	int	write_signal;
	int	state;	/* 0 = idle  >0 = # of readers  -1 = writer */
	int	blocked_writers;
};

int sysv_mutex_init(struct sysv_mutex *);
int sysv_mutex_lock(struct sysv_mutex *);
int sysv_mutex_unlock(struct sysv_mutex *);

int sysv_rwlock_init(struct sysv_rwlock *);
int sysv_rwlock_unlock(struct sysv_rwlock *);
int sysv_rwlock_wrlock(struct sysv_rwlock *);
int sysv_rwlock_rdlock(struct sysv_rwlock *);

#endif

