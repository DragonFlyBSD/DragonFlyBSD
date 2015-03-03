/*
 * Copyright (c) 1981, 1984, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
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
 *	@(#)msgbuf.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD: src/sys/sys/msgbuf.h,v 1.14.2.1 2001/01/16 12:26:21 phk Exp $
 * $DragonFly: src/sys/sys/msgbuf.h,v 1.6 2006/09/30 20:03:44 swildner Exp $
 */

#ifndef _SYS_MSGBUF_H_
#define _SYS_MSGBUF_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

/*
 * NOTE!  The indices are not masked against msg_size.  All accessors
 *	  must mask the indices against msg_size to get actual buffer
 *	  indexes.  For relative block sizes, simple subtraction can be
 *	  used using unsigned integers.
 */
struct	msgbuf {
#define	MSG_MAGIC	0x063064
#define	MSG_OMAGIC	0x063062
	unsigned int	msg_magic;
	unsigned int	msg_size;		/* size of buffer area */
	unsigned int	msg_bufx;		/* write index - kernel */
	unsigned int	msg_bufr;		/* base index  - kernel */
	char * 		msg_ptr;		/* pointer to buffer */
	unsigned int	msg_bufl;		/* read index - log device */
	unsigned int	msg_unused01;
};

#ifdef _KERNEL
extern int	msgbuftrigger;
extern struct	msgbuf *msgbufp;
void	msgbufinit	(void *ptr, size_t size);

#if !defined(MSGBUF_SIZE)
#define	MSGBUF_SIZE	131072
#endif

#endif

#endif
