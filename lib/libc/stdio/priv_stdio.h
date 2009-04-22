/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2005 Joerg Sonnenberger <joerg@bec.de>.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 * $DragonFly: src/lib/libc/stdio/priv_stdio.h,v 1.3 2005/08/02 00:44:39 joerg Exp $
 */

#ifndef _LIBC_PRIV_STDIO_H_
#define _LIBC_PRIV_STDIO_H_

#include <pthread.h>

#include "wcio.h"

/*
 * NB: to fit things in six character monocase externals, the stdio
 * code uses the prefix `__s' for stdio objects, typically followed
 * by a three-character attempt at a mnemonic.
 */

/* stdio buffers */
struct __sbuf {
	unsigned char *_base;
	int	_size;
};

/*
 * _ub, _up, and _ur are used when ungetc() pushes back more characters
 * than fit in the current _bf, or when ungetc() pushes back a character
 * that does not match the previous one in _bf.  When this happens,
 * _ub._base becomes non-nil (i.e., a stream has ungetc() data iff
 * _ub._base!=NULL) and _up and _ur save the current values of _p and _r.
 */

struct __FILE {
	struct __FILE_public	pub;
	struct	__sbuf _bf;	/* the buffer (at least 1 byte, if !NULL) */

	/* operations */
	void	*_cookie;	/* cookie passed to io functions */
	int	(*_close)(void *);
	int	(*_read)(void *, char *, int);
	fpos_t	(*_seek)(void *, fpos_t, int);
	int	(*_write)(void *, const char *, int);

	/* separate buffer for long sequences of ungetc() */
	struct	__sbuf _ub;	/* ungetc buffer */
	int	_ur;		/* saved _r when _r is counting ungetc data */

	/* tricks to meet minimum requirements even when malloc() fails */
	unsigned char _ubuf[3];	/* guarantee an ungetc() buffer */
	unsigned char _nbuf[1];	/* guarantee a getc() buffer */

	/* separate buffer for fgetln() when line crosses buffer boundary */
	struct	__sbuf _lb;	/* buffer for fgetln() */

	/* Unix stdio files get aligned to block boundaries on fseek() */
	int	_blksize;	/* stat.st_blksize (may be != _bf._size) */
	fpos_t	_offset;	/* current lseek offset (see WARNING) */

	unsigned char	*_up;	/* saved _p when _p is doing ungetc data */
	pthread_mutex_t	_fl_mutex;	/* used for MT-safety */
	pthread_t	_fl_owner;	/* current owner */
	int		_fl_count;	/* recursive lock count */

	struct wchar_io_data _wcio;
};

/*
 * I/O descriptors for __sfvwrite().
 */
struct __siov {
	void	*iov_base;
	size_t	iov_len;
};
struct __suio {
	struct	__siov *uio_iov;
	int	uio_iovcnt;
	int	uio_resid;
};

/*
 * The first few FILEs are statically allocated; others are dynamically
 * allocated and linked in via this glue structure.
 */
struct glue {
	struct	glue *next;
	int	niobs;
	FILE	*iobs;
};

extern struct glue __sglue;

__BEGIN_DECLS
int	__sfvwrite(FILE *, struct __suio *);
int	__fflush(FILE *);
int	__ungetc(int, FILE *);
__END_DECLS

#endif /* _LIBC_PRIV_STDIO_H_ */
