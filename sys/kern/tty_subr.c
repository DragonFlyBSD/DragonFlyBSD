/*
 * Copyright (c) 1994, David Greenman
 * All rights reserved.
 * Copyright (c) 2003-2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
  * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * clist support routines
 *
 * The clist now contains two linear buffers c_quote and c_info, sized
 * to c_cbmax.  The caller must hold a lock or token specific to the clist
 * being manipulated.
 */
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/tty.h>

/*
 * Allocate or reallocate clist buffers.
 */
void
clist_alloc_cblocks(struct clist *cl, int ccmax)
{
	short *data;
	int count;
	int n;

	if (ccmax == cl->c_ccmax)
		return;
	if (ccmax == 0) {
		clist_free_cblocks(cl);
		return;
	}
	data = kmalloc(ccmax * sizeof(*data), M_TTYS, M_INTWAIT|M_ZERO);
	/* NOTE: cl fields may now be different due to blocking */

	count = cl->c_cc;
	if (cl->c_cc) {
		if (count > ccmax)
			count = ccmax;
		n = cl->c_ccmax - cl->c_cchead;
		if (n > count)
			n = count;
		bcopy(cl->c_data + cl->c_cchead, data, n * sizeof(*data));
		if (n < count) {
			bcopy(cl->c_data, data + n,
			      (count - n) * sizeof(*data));
		}
	}
	cl->c_cc = count;
	cl->c_ccmax = ccmax;
	cl->c_cchead = 0;
	cl->c_data = data;
}

/*
 * Free the clist's buffer.
 */
void
clist_free_cblocks(struct clist *cl)
{
	short *data;

	data = cl->c_data;

	cl->c_cc = 0;
	cl->c_ccmax = 0;
	cl->c_cchead = 0;
	cl->c_unused01 = 0;
	cl->c_data = NULL;
	if (data)
		kfree(data, M_TTYS);
}

/*
 * Get a character from the head of a clist.
 */
int
clist_getc(struct clist *cl)
{
	short c;
	int i;

	if (cl->c_cc == 0)
		return -1;
	i = cl->c_cchead;
	c = cl->c_data[i];
	if (++i == cl->c_ccmax)
		i = 0;
	cl->c_cchead = i;
	--cl->c_cc;
	return ((int)c);
}

/*
 * Copy data from the clist to the destination linear buffer.
 * Return the number of characters actually copied.
 */
int
clist_qtob(struct clist *cl, char *dest, int n)
{
	int count;
	int i;
	short c;

	if (n > cl->c_cc)
		n = cl->c_cc;
	count = n;
	i = cl->c_cchead;

	while (n) {
		c = cl->c_data[i];
		if (++i == cl->c_ccmax)
			i = 0;
		*dest++ = (char)c;
		--n;
	}
	cl->c_cchead = i;
	cl->c_cc -= count;

	return count;
}

/*
 * Flush characters from the head of the clist, deleting them.
 */
void
ndflush(struct clist *cl, int n)
{
	int i;

	if (n > cl->c_cc)
		n = cl->c_cc;
	i = cl->c_cchead + n;
	if (i >= cl->c_ccmax)
		i -= cl->c_ccmax;
	cl->c_cchead = i;
	cl->c_cc -= n;
}

/*
 * Append a character to the clist, return 0 on success, -1 if
 * there is no room.  The character can be quoted by setting TTY_QUOTE.
 */
int
clist_putc(int c, struct clist *cl)
{
	int i;

	if (cl->c_cc == cl->c_ccmax)
		return -1;
	i = cl->c_cchead + cl->c_cc;
	if (i >= cl->c_ccmax)
		i -= cl->c_ccmax;
	cl->c_data[i] = (short)c & (TTY_QUOTE | TTY_CHARMASK);
	++cl->c_cc;

	return 0;
}

/*
 * Copy data from linear buffer to clist chain.  Return the
 * number of characters not copied.  The data will be flagged
 * as not being quoted.
 */
int
clist_btoq(char *src, int n, struct clist *cl)
{
	int i;
	int count;
	int remain;

	count = cl->c_ccmax - cl->c_cc;		/* space available */
	if (count > n)
		count = n;			/* count = bytes to copy */
	remain = n - count;			/* remain = bytes not copied */

	i = cl->c_cchead + cl->c_cc;		/* clist write index */
	if (i >= cl->c_ccmax)
		i -= cl->c_ccmax;

	while (count) {
		cl->c_data[i] = (short)(uint8_t)*src;
		if (++i == cl->c_ccmax)
			i = 0;
		++src;
		--count;
	}
	cl->c_cc += n - remain;			/* bytes actually copied */

	return remain;				/* return bytes not copied */
}

/*
 * Get the next character in the clist relative to cp.  If cp is NULL
 * returns the first character in the clist.  The character is stored in
 * *dst.  No clist pointers are advanced or adjusted.
 *
 * The returned pointer can be used as an iterator but should not be
 * directly dereferenced.
 */
void *
clist_nextc(struct clist *cl, void *cp, int *dst)
{
	int i;

	if (cp == NULL) {
		if (cl->c_cc == 0) {
			*dst = -1;
			return NULL;
		}
		cp = &cl->c_data[cl->c_cchead];
		*dst = (uint16_t)*(short *)cp;	/* can be quoted */
		return cp;
	}

	/*
	 * Use i to calculate the next logical index to determine if
	 * there are any characters remaining.
	 */
	i = (short *)cp - cl->c_data;
	if (i < cl->c_cchead)
		i += cl->c_ccmax - cl->c_cchead;
	else
		i -= cl->c_cchead;
	if (i + 1 == cl->c_cc) {		/* no more chars */
		*dst = 0;
		return NULL;
	}

	/*
	 * We can just use cp to iterate the next actual buffer
	 * position.
	 */
	cp = (short *)cp + 1;			/* next char (use pointer) */
	if (cp == &cl->c_data[cl->c_ccmax])
		cp = &cl->c_data[0];
	*dst = (uint16_t)*(short *)cp;

	return cp;
}

/*
 * "Unput" a character from a clist, returning it.
 */
int
clist_unputc(struct clist *cl)
{
	int c;
	int i;

	if (cl->c_cc == 0)
		return -1;
	--cl->c_cc;
	i = cl->c_cchead + cl->c_cc;
	if (i >= cl->c_ccmax)
		i -= cl->c_ccmax;
	c = (int)(uint16_t)cl->c_data[i];

	return c;
}

/*
 * Move characters in source clist to destination clist,
 * preserving quote bits.  Non-critical path.
 */
void
clist_catq(struct clist *cls, struct clist *cld)
{
	int c;

	while ((c = clist_getc(cls)) != -1)
		clist_putc(c, cld);
}
