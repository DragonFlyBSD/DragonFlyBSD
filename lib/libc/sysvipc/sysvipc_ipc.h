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

#ifndef _SYSV_IPC_H_
#define _SYSV_IPC_H_

#define MAXSIZE		100

#define IPC_INITIALIZED	1

#define SHMGET		1
#define SEMGET		2
#define MSGGET		3
#define UNDOGET		4

#define SHMAT		5
#define SHMDT		6
#define SHMCTL		7

#define SEMOP		8
#define SEMCTL		9

#define PIPE_READ	0
#define PIPE_WRITE	1

#define	IPCID_TO_IX(id)		((id) & 0xffff)
#define	IPCID_TO_SEQ(id)	(((id) >> 16) & 0xffff)
#define	IXSEQ_TO_IPCID(ix,perm)	(((perm.seq) << 16) | (ix & 0xffff))

#include <sys/shm.h>
#include "sysvipc_utils.h"

/* Structures used to send/receive
 * messages to/from daemon.
 */
struct shmget_msg {
	key_t key;
	size_t size;
	int shmflg;
	int type;
};

struct shmat_msg {
	int shmid;
	const void *shmaddr;
	int shmflg;
	size_t size;
};

struct shmdt_msg {
	const void *shmaddr;
};

struct shmctl_msg {
	int shmid;
	int cmd;
	struct shmid_ds buf;
};

struct sysvipc_msg {
	int type;
	char data[0];
};

struct semget_msg {
	key_t key;
	int nsems;
	int shmflg;
};

/* Send/receive messages. */
int send_message(int, int, char *, int);
int receive_type_message(int);
int receive_message(int, char *, int);

/* sysv ipc structures initialization. */
int is_sysvinit(void);
int sysvinit(void);
int sysvexit(void);

#endif
