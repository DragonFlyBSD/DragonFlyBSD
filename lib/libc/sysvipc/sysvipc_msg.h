/* $FreeBSD: src/sys/kern/sysv_msg.c,v 1.23.2.5 2002/12/31 08:54:53 maxim Exp $ */

/*
 * Implementation of SVID messages
 *
 * Author:  Daniel Boulet
 *
 * Copyright 1993 Daniel Boulet and RTMX Inc.
 * Copyright (c) 2013 Larisa Grigore <larisagrigore@gmail.com>
 *
 * This system call was implemented by Daniel Boulet under contract from RTMX.
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 */

#ifndef _SYSV_MSG_H_
#define _SYSV_MSG_H_

#include <sys/msg.h>
#include "sysvipc_lock.h"
#include "sysvipc_lock_generic.h"

struct msg {
	short	msg_next;	/* next msg in the chain */
	long	msg_type;	/* type of this message */
    				/* >0 -> type of this message */
    				/* 0 -> free header */
	u_short	msg_ts;		/* size of this message */
	short	msg_spot;	/* location of start of msg in buffer */
};

/* Intarnal structure defined to keep compatbility with
 * th kernel implementation.
 */
struct msqid_ds_internal {
	struct	ipc_perm msg_perm;	/* msg queue permission bits */
	union {
		struct	msg *msg_first;
		int msg_first_index;
	} first;
	union {
		struct	msg *msg_last;
		int msg_last_index;
	} last;
	u_long	msg_cbytes;	/* number of bytes in use on the queue */
	u_long	msg_qnum;	/* number of msgs in the queue */
	u_long	msg_qbytes;	/* max # of bytes on the queue */
	pid_t	msg_lspid;	/* pid of last msgsnd() */
	pid_t	msg_lrpid;	/* pid of last msgrcv() */
	time_t	msg_stime;	/* time of last msgsnd() */
	long	msg_pad1;
	time_t	msg_rtime;	/* time of last msgrcv() */
	long	msg_pad2;
	time_t	msg_ctime;	/* time of last msgctl() */
	long	msg_pad3;
	long	msg_pad4[4];
};

#ifndef MSGSSZ
#define MSGSSZ	8		/* Each segment must be 2^N long */
#endif
#ifndef MSGSEG
#define MSGSEG	256		/* must be calculated such that all
					structure fits in PAGE_SIZE. */
#endif
#define MSGMAX	(MSGSSZ*MSGSEG)
#ifndef MSGMNB
#define MSGMNB	2048		/* max # of bytes in a queue */
#endif
#ifndef MSGMNI
#define MSGMNI	40
#endif
#ifndef MSGTQL
#define MSGTQL	10//40
#endif

 /* Each message is broken up and stored in segments that are msgssz bytes
 * long.  For efficiency reasons, this should be a power of two.  Also,
 * it doesn't make sense if it is less than 8 or greater than about 256.
 * Consequently, msginit in kern/sysv_msg.c checks that msgssz is a power of
 * two between 8 and 1024 inclusive (and panic's if it isn't).
 */
struct msginfo {
	int	msgmax,		/* max chars in a message */
		msgmni,		/* max message queue identifiers */
		msgmnb,		/* max chars in a queue */
		msgtql,		/* max messages in system */
		msgssz,		/* size of a message segment (see notes above) */
		msgseg;		/* number of message segments */
};

struct msgmap {
	short	next;		/* next segment in buffer */
    				/* -1 -> available */
    				/* 0..(MSGSEG-1) -> index of next segment */
};

struct msqid_pool {
#ifdef SYSV_RWLOCK
	struct sysv_rwlock rwlock;
#else
	struct sysv_mutex mutex;
#endif
	struct msqid_ds_internal ds;
	char gen;
	int nfree_msgmaps;	/* # of free map entries */
	short free_msgmaps;	/* head of linked list of free map entries */
	short free_msghdrs;/* list of free msg headers */
	struct msg msghdrs[MSGTQL];	/* MSGTQL msg headers */
	struct msgmap msgmaps[MSGSEG];	/* MSGSEG msgmap structures */
	char msgpool[MSGMAX];		/* MSGMAX byte long msg buffer pool */
};

int sysvipc_msgctl (int, int, struct msqid_ds *);
int sysvipc_msgget (key_t, int);
int sysvipc_msgsnd (int, void *, size_t, int);
int sysvipc_msgrcv (int, void*, size_t, long, int);

#endif /* !_SYSV_MSG_H_ */
