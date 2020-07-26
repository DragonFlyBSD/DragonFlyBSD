/*
 * Copyright (c) 1996 John S. Dyson
 * All rights reserved.
 * Copyright (c) 2003-2017 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Absolutely no warranty of function or purpose is made by the author
 *    John S. Dyson.
 * 4. This work was done expressly for inclusion into FreeBSD.  Other use
 *    is allowed if this notation is included.
 * 5. Modifications may be freely made to this file if the above conditions
 *    are met.
 */

#ifndef _SYS_PIPE_H_
#define _SYS_PIPE_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_TIME_H_
#include <sys/time.h>			/* for struct timespec */
#endif
#ifndef _SYS_EVENT_H_
#include <sys/event.h>			/* for struct kqinfo */
#endif
#ifndef _SYS_XIO_H_
#include <sys/xio.h>			/* for struct xio */
#endif
#ifndef _SYS_THREAD_H_
#include <sys/thread.h>			/* for struct lwkt_token */
#endif
#ifndef _MACHINE_PARAM_H_
#include <machine/param.h>		/* for PAGE_SIZE */
#endif

/*
 * Pipe buffer information.
 */
struct pipebuf {
	struct {
		struct lwkt_token rlock;
		size_t		rindex;	/* current read index (FIFO)	*/
		int32_t		rip;	/* blocking read requested (FIFO) */
		int32_t		unu01;	/* blocking read requested (FIFO) */
		struct timespec	atime;	/* time of last access */
	} __cachealign;
	struct {
		struct lwkt_token wlock;
		size_t		windex;	/* current write index (FIFO)	*/
		int32_t		wip;
		int32_t		unu02;
		struct timespec	mtime;	/* time of last modify */
	} __cachealign;
	size_t		size;		/* size of buffer */
	caddr_t		buffer;		/* kva of buffer */
	struct vm_object *object;	/* VM object containing buffer */
	struct kqinfo	kq;		/* for compat with select/poll/kq */
	struct sigio	*sigio;		/* information for async I/O */
	uint32_t	state;		/* pipe status info */
	int		lticks;		/* vfs_timestamp optimization */
} __cachealign;

/*
 * Bits in pipebuf.state.
 */
#define PIPE_ASYNC	0x0004	/* Async? I/O */
#define PIPE_WANTR	0x0008	/* Reader wants some characters */
#define PIPE_WANTW	0x0010	/* Writer wants space to put characters */
#define PIPE_REOF	0x0040	/* Pipe is in EOF condition (read EOF) */
#define PIPE_WEOF	0x0080	/* Pipe is in EOF condition (write shutdown) */
#define PIPE_CLOSED	0x1000	/* Pipe has been closed */

/*
 * The pipe() data structure encompasses two pipes.  Bit 0 in fp->f_data
 * denotes which.
 */
struct pipe {
	struct pipebuf	bufferA;	/* data storage */
	struct pipebuf	bufferB;	/* data storage */
	struct timespec	ctime;		/* time of status change */
	struct pipe	*next;
	uint32_t	open_count;
	uint32_t	unused01;
	uint64_t	inum;
} __cachealign;

#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#endif /* !_SYS_PIPE_H_ */
