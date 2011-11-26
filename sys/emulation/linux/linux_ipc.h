/*-
 * Copyright (c) 2000 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
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
 *
 * $FreeBSD: src/sys/compat/linux/linux_ipc.h,v 1.2.2.4 2001/11/05 19:08:22 marcel Exp $
 * $DragonFly: src/sys/emulation/linux/linux_ipc.h,v 1.7 2003/11/20 06:05:29 dillon Exp $
 */

#ifndef _LINUX_IPC_H_
#define _LINUX_IPC_H_

#ifdef __i386__


struct l_msginfo {
	l_int msgpool;
	l_int msgmap;
	l_int msgmax;
	l_int msgmnb;
	l_int msgmni;
	l_int msgssz;
	l_int msgtql;
	l_ushort msgseg;
};

struct linux_msgctl_args 
{
	struct sysmsg	sysmsg;
	l_int		msqid;
	l_int		cmd;
	struct l_msqid_ds *buf;
};

struct linux_msgget_args
{
	struct sysmsg	sysmsg;
	l_key_t		key;
	l_int		msgflg;
};

struct linux_msgrcv_args
{
	struct sysmsg	sysmsg;
	l_int		msqid;
	struct l_msgbuf *msgp;
	l_size_t	msgsz;
	l_long		msgtyp;
	l_int		msgflg;
};

struct linux_msgsnd_args
{
	struct sysmsg	sysmsg;
	l_int		msqid;
	struct l_msgbuf *msgp;
	l_size_t	msgsz;
	l_int		msgflg;
};

struct linux_semctl_args
{
	struct sysmsg	sysmsg;
	l_int		semid;
	l_int		semnum;
	l_int		cmd;
	union l_semun	arg;
};

struct linux_semget_args
{
	struct sysmsg	sysmsg;
	l_key_t		key;
	l_int		nsems;
	l_int		semflg;
};

struct linux_semop_args
{
	struct sysmsg	sysmsg;
	l_int		semid;
	struct l_sembuf *tsops;
	l_uint		nsops;
};

struct linux_shmat_args
{
	struct sysmsg	sysmsg;
	l_int		shmid;
	char		*shmaddr;
	l_int		shmflg;
	l_ulong		*raddr;
};

struct linux_shmctl_args
{
	struct sysmsg	sysmsg;
	l_int		shmid;
	l_int		cmd;
	struct l_shmid_ds *buf;
};

struct linux_shmdt_args
{
	struct sysmsg	sysmsg;
	char *shmaddr;
};

struct linux_shmget_args
{
	struct sysmsg	sysmsg;
	l_key_t		key;
	l_size_t	size;
	l_int		shmflg;
};



struct l_seminfo {
	l_int semmap;
	l_int semmni;
	l_int semmns;
	l_int semmnu;
	l_int semmsl;
	l_int semopm;
	l_int semume;
	l_int semusz;
	l_int semvmx;
	l_int semaem;
};


struct l_shminfo {
	l_int shmmax;
	l_int shmmin;
	l_int shmmni;
	l_int shmseg;
	l_int shmall;
};

struct l_shm_info {
	l_int used_ids;
	l_ulong shm_tot;  /* total allocated shm */
	l_ulong shm_rss;  /* total resident shm */
	l_ulong shm_swp;  /* total swapped shm */
	l_ulong swap_attempts;
	l_ulong swap_successes;
};


struct l_ipc64_perm
{
	l_key_t		key;
	l_uid_t		uid;
	l_gid_t		gid;
	l_uid_t		cuid;
	l_gid_t		cgid;
	l_mode_t	mode;
	l_ushort	__pad1;
	l_ushort	seq;
	l_ushort	__pad2;
	l_ulong		__unused1;
	l_ulong		__unused2;
};

/*
 * The msqid64_ds structure for i386 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct l_msqid64_ds {
	struct l_ipc64_perm msg_perm;
	l_time_t	msg_stime;	/* last msgsnd time */
	l_ulong		__unused1;
	l_time_t	msg_rtime;	/* last msgrcv time */
	l_ulong		__unused2;
	l_time_t	msg_ctime;	/* last change time */
	l_ulong		__unused3;
	l_ulong		msg_cbytes;	/* current number of bytes on queue */
	l_ulong		msg_qnum;	/* number of messages in queue */
	l_ulong		msg_qbytes;	/* max number of bytes on queue */
	l_pid_t		msg_lspid;	/* pid of last msgsnd */
	l_pid_t		msg_lrpid;	/* last receive pid */
	l_ulong		__unused4;
	l_ulong		__unused5;
};

/*
 * The semid64_ds structure for i386 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct l_semid64_ds {
	struct l_ipc64_perm sem_perm;	/* permissions */
	l_time_t	sem_otime;	/* last semop time */
	l_ulong		__unused1;
	l_time_t	sem_ctime;	/* last change time */
	l_ulong		__unused2;
	l_ulong		sem_nsems;	/* no. of semaphores in array */
	l_ulong		__unused3;
	l_ulong		__unused4;
};

/*
 * The shmid64_ds structure for i386 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct l_shmid64_ds {
	struct l_ipc64_perm shm_perm;	/* operation perms */
	l_size_t	shm_segsz;	/* size of segment (bytes) */
	l_time_t	shm_atime;	/* last attach time */
	l_ulong		__unused1;
	l_time_t	shm_dtime;	/* last detach time */
	l_ulong		__unused2;
	l_time_t	shm_ctime;	/* last change time */
	l_ulong		__unused3;
	l_pid_t		shm_cpid;	/* pid of creator */
	l_pid_t		shm_lpid;	/* pid of last operator */
	l_ulong		shm_nattch;	/* no. of current attaches */
	l_ulong		__unused4;
	l_ulong		__unused5;
};

struct l_shminfo64 {
	l_ulong   	shmmax;
	l_ulong   	shmmin;
	l_ulong   	shmmni;
	l_ulong   	shmseg;
	l_ulong   	shmall;
	l_ulong   	__unused1;
	l_ulong   	__unused2;
	l_ulong   	__unused3;
	l_ulong   	__unused4;
};


struct l_ipc_perm {
	l_key_t		key;
	l_uid16_t	uid;
	l_gid16_t	gid;
	l_uid16_t	cuid;
	l_gid16_t	cgid;
	l_ushort	mode;
	l_ushort	seq;
};

struct l_msqid_ds {
	struct l_ipc_perm	msg_perm;
	l_uintptr_t		msg_first;	/* first message on queue,unused */
	l_uintptr_t		msg_last;	/* last message in queue,unused */
	l_time_t		msg_stime;	/* last msgsnd time */
	l_time_t		msg_rtime;	/* last msgrcv time */
	l_time_t		msg_ctime;	/* last change time */
	l_ulong			msg_lcbytes;	/* Reuse junk fields for 32 bit */
	l_ulong			msg_lqbytes;	/* ditto */
	l_ushort		msg_cbytes;	/* current number of bytes on queue */
	l_ushort		msg_qnum;	/* number of messages in queue */
	l_ushort		msg_qbytes;	/* max number of bytes on queue */
	l_pid_t			msg_lspid;	/* pid of last msgsnd */
	l_pid_t			msg_lrpid;	/* last receive pid */
};

struct l_semid_ds {
	struct l_ipc_perm	sem_perm;
	l_time_t		sem_otime;
	l_time_t		sem_ctime;
	void			*sem_base;
	void			*sem_pending;
	void			*sem_pending_last;
	void			*undo;
	l_ushort		sem_nsems;
};

struct l_shmid_ds {
	struct l_ipc_perm	shm_perm;
	l_int			shm_segsz;
	l_time_t		shm_atime;
	l_time_t		shm_dtime;
	l_time_t		shm_ctime;
	l_ushort		shm_cpid;
	l_ushort		shm_lpid;
	l_short			shm_nattch;
	l_ushort		private1;
	void			*private2;
	void			*private3;
};

int linux_msgctl (struct linux_msgctl_args *);
int linux_msgget (struct linux_msgget_args *);
int linux_msgrcv (struct linux_msgrcv_args *);
int linux_msgsnd (struct linux_msgsnd_args *);

int linux_semctl (struct linux_semctl_args *);
int linux_semget (struct linux_semget_args *);
int linux_semop  (struct linux_semop_args *);

int linux_shmat  (struct linux_shmat_args *);
int linux_shmctl (struct linux_shmctl_args *);
int linux_shmdt  (struct linux_shmdt_args *);
int linux_shmget (struct linux_shmget_args *);
#define	LINUX_MSG_INFO	12
#define	LINUX_IPC_64	0x0100	/* New version (support 32-bit UIDs, bigger */
#endif	/* __i386__ */

#endif /* _LINUX_IPC_H_ */
