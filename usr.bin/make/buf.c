/*-
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1988, 1989 by Adam de Boor
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
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
 * @(#)buf.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/make/buf.c,v 1.11 1999/09/11 13:08:01 hoek Exp $
 * $DragonFly: src/usr.bin/make/buf.c,v 1.21 2005/01/24 05:07:34 okumoto Exp $
 */

/**
 * buf.c
 *	Functions for automatically-expanded buffers.
 */

#include <string.h>
#include <stdlib.h>

#include "buf.h"
#include "sprite.h"
#include "util.h"

#ifndef MAX
#define	MAX(a,b)  ((a) > (b) ? (a) : (b))
#endif

/**
 * Returns the number of bytes in the given buffer.  Doesn't include
 * the null-terminating byte.
 *
 * @return The number of bytes in Buffer object.
 */
inline size_t
Buf_Size(const Buffer *buf)
{
	return (buf->inPtr - buf->buffer);
}

/**
 * Expand the given buffer to hold the given number of additional
 * bytes, plus space to store a terminating NULL byte.
 */
static inline void
BufExpand(Buffer *bp, size_t nb)
{
	size_t	len = Buf_Size(bp);
	if (bp->size < len + nb + 1) {
		int size = bp->size + MAX(nb + 1, BUF_ADD_INC);

		bp->size	= size;
		bp->buffer	= erealloc(bp->buffer, size);
		bp->inPtr	= bp->buffer + len;
	}
}

/* Buf_AddByte adds a single byte to a buffer. */
void
Buf_AddByte(Buffer *bp, Byte byte)
{
	BufExpand(bp, 1);

	*bp->inPtr = byte;
	bp->inPtr++;
	*bp->inPtr = '\0';
}

/*-
 *-----------------------------------------------------------------------
 * Buf_AddBytes
 *	Add a number of bytes to the buffer.
 *-----------------------------------------------------------------------
 */
void
Buf_AddBytes(Buffer *bp, size_t numBytes, const Byte *bytesPtr)
{
	BufExpand(bp, numBytes);

	memcpy(bp->inPtr, bytesPtr, numBytes);
	bp->inPtr += numBytes;
	*bp->inPtr = '\0';
}

/**
 * Get a reference to the internal buffer.
 *
 * @return A pointer to the data and the number of bytes available.
 */
Byte *
Buf_GetAll(Buffer *bp, size_t *numBytesPtr)
{
	if (numBytesPtr != NULL)
		*numBytesPtr = Buf_Size(bp);

	return (bp->buffer);
}

/**
 * Initialize a buffer. If no initial size is given, a reasonable
 * default is used.
 *
 * @return A buffer object to be given to other functions in this library.
 *
 * Side Effects:
 *	Space is allocated for the Buffer object and a internal buffer.
 */
Buffer *
Buf_Init(size_t size)
{
	Buffer *bp;	/* New Buffer */

	if (size <= 0)
		size = BUF_DEF_SIZE;

	bp = emalloc(sizeof(*bp));
	bp->size	= size;
	bp->buffer	= emalloc(size);
	bp->inPtr	= bp->buffer;
	*bp->inPtr	= '\0';

	return (bp);
}

/**
 * Buf_Destroy
 *	Destroy a buffer, and optionally free its data, too.
 *
 * Side Effects:
 *      Space for the Buffer object and possibly the internal buffer
 *	is de-allocated.
 */
void
Buf_Destroy(Buffer *buf, Boolean freeData)
{
	if (freeData)
		free(buf->buffer);
	free(buf);
}

/**
 * Buf_ReplaceLastByte
 *      Replace the last byte in a buffer.  If the buffer was empty
 *	intially, then a new byte will be added.
 */
void
Buf_ReplaceLastByte(Buffer *bp, Byte byte)
{
	if (bp->inPtr == bp->buffer) {
		*bp->inPtr	= byte;
		bp->inPtr++;
		*bp->inPtr	= '\0';
	} else {
		*(bp->inPtr - 1) = byte;
	}
}

void
Buf_Clear(Buffer *bp)
{
	bp->inPtr	= bp->buffer;
	*bp->inPtr	= '\0';
}

