/**
 * Copyright (c) 2013 Larisa Grigore <larisagrigore@gmail.com>.
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

#ifndef SYSVD_UTILS_H
#define SYSVD_UTILS_H

#define SOCKET_FD_IDX		0

#define POLLPIPE	(POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)

#define CONNEXION_CLOSED	0

#define	SHMSEG_FREE     	0x0200
#define	SHMSEG_REMOVED  	0x0400
#define	SHMSEG_ALLOCATED	0x0800
#define	SHMSEG_WANTED		0x1000

#define DIRPATH		"/var/run/sysvipc"

#define SHM_NAME	"shm"
#define SEM_NAME	"sem"
#define MSG_NAME	"msg"
#define UNDO_NAME	"undo"

#include <sys/queue.h>

struct client {
	//int fd[2];
	int sock;
	pid_t pid;
	int undoid;
	LIST_HEAD(_ids_attached, id_attached) ids_attached;
};

struct client_entry {
	struct client			*client;
	LIST_ENTRY(client_entry)	client_link;
};

LIST_HEAD(client_hashtable, client_entry) *clienthashtable;
u_long clientmask;

struct pid_attached {
	int pid;
	LIST_ENTRY(pid_attached)	link;
};

struct id_attached {
	int shmid;
	LIST_ENTRY(id_attached)	link;
};

struct shm_handle {
	int type;
	int fd;
	LIST_HEAD(_attached_list, pid_attached) attached_list;
};

/* Print wrappers. */
void sysvd_print_err(const char *fmt, ...);
void sysvd_print(const char *fmt, ...);

#endif
