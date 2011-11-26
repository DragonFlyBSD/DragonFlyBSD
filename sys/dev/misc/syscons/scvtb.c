/*-
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/syscons/scvtb.c,v 1.5.2.1 2001/07/16 05:21:23 yokota Exp $
 * $DragonFly: src/sys/dev/misc/syscons/scvtb.c,v 1.9 2006/09/05 03:48:10 dillon Exp $
 */

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/console.h>
#include <machine/md_var.h>

#include <dev/video/fb/fbreg.h>
#include "syscons.h"

#define vtb_wrap(vtb, at, offset)				\
    (((at) + (offset) + (vtb)->vtb_size)%(vtb)->vtb_size)

void
sc_vtb_init(sc_vtb_t *vtb, int type, int cols, int rows, void *buf, int wait)
{
	vtb->vtb_flags = 0;
	vtb->vtb_type = type;
	vtb->vtb_cols = cols;
	vtb->vtb_rows = rows;
	vtb->vtb_size = cols*rows;
	vtb->vtb_buffer = NULL;
	vtb->vtb_tail = 0;

	switch (type) {
	case VTB_MEMORY:
	case VTB_RINGBUFFER:
		if ((buf == NULL) && (cols*rows != 0)) {
			vtb->vtb_buffer = kmalloc(cols*rows*sizeof(uint16_t),
				    M_SYSCONS,
				    M_ZERO | ((wait) ? M_WAITOK : M_NOWAIT));
			if (vtb->vtb_buffer != NULL) {
				vtb->vtb_flags |= VTB_VALID;
				vtb->vtb_flags |= VTB_ALLOCED;
			}
		} else {
			vtb->vtb_buffer = buf;
			vtb->vtb_flags |= VTB_VALID;
		}
		break;
	case VTB_FRAMEBUFFER:
		vtb->vtb_buffer = buf;
		vtb->vtb_flags |= VTB_VALID;
		break;
	default:
		break;
	}
}

void
sc_vtb_destroy(sc_vtb_t *vtb)
{
	uint16_t *p;

	vtb->vtb_cols = 0;
	vtb->vtb_rows = 0;
	vtb->vtb_size = 0;
	vtb->vtb_tail = 0;

	p = vtb->vtb_buffer;
	vtb->vtb_buffer = NULL;
	switch (vtb->vtb_type) {
	case VTB_MEMORY:
	case VTB_RINGBUFFER:
		if ((vtb->vtb_flags & VTB_ALLOCED) && (p != NULL))
			kfree(p, M_SYSCONS);
		break;
	default:
		break;
	}
	vtb->vtb_flags = 0;
	vtb->vtb_type = VTB_INVALID;
}

size_t
sc_vtb_size(int cols, int rows)
{
	return (size_t)(cols*rows*sizeof(uint16_t));
}

int
sc_vtb_getc(sc_vtb_t *vtb, int at)
{
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		return (readw(vtb->vtb_buffer + at) & 0x00ff);
	else
		return (*(vtb->vtb_buffer + at) & 0x00ff);
}

int
sc_vtb_geta(sc_vtb_t *vtb, int at)
{
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		return (readw(vtb->vtb_buffer + at) & 0xff00);
	else
		return (*(vtb->vtb_buffer + at) & 0xff00);
}

void
sc_vtb_putc(sc_vtb_t *vtb, int at, int c, int a)
{
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		writew(vtb->vtb_buffer + at, a | c);
	else
		*(vtb->vtb_buffer + at) = a | c;
}

uint16_t *
sc_vtb_putchar(sc_vtb_t *vtb, uint16_t *p, int c, int a)
{
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		writew(p, a | c);
	else
		*p = a | c;
	return (p + 1);
}

int
sc_vtb_pos(sc_vtb_t *vtb, int pos, int offset)
{
	return ((pos + offset + vtb->vtb_size)%vtb->vtb_size);
}

void
sc_vtb_clear(sc_vtb_t *vtb, int c, int attr)
{
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		fillw_io(attr | c, vtb->vtb_buffer, vtb->vtb_size);
	else
		fillw(attr | c, vtb->vtb_buffer, vtb->vtb_size);
}

void
sc_vtb_copy(sc_vtb_t *vtb1, int from, sc_vtb_t *vtb2, int to, int count)
{
	/* XXX if both are VTB_VRAMEBUFFER... */
	if (vtb2->vtb_type == VTB_FRAMEBUFFER) {
		bcopy_toio(vtb1->vtb_buffer + from, vtb2->vtb_buffer + to,
			   count*sizeof(uint16_t));
	} else if (vtb1->vtb_type == VTB_FRAMEBUFFER) {
		bcopy_fromio(vtb1->vtb_buffer + from, vtb2->vtb_buffer + to,
			     count*sizeof(uint16_t));
	} else {
		bcopy(vtb1->vtb_buffer + from, vtb2->vtb_buffer + to,
		      count*sizeof(uint16_t));
	}
}

void
sc_vtb_append(sc_vtb_t *vtb1, int from, sc_vtb_t *vtb2, int count)
{
	int len;

	if (vtb2->vtb_type != VTB_RINGBUFFER)
		return;

	while (count > 0) {
		len = imin(count, vtb2->vtb_size - vtb2->vtb_tail);
		if (vtb1->vtb_type == VTB_FRAMEBUFFER) {
			bcopy_fromio(vtb1->vtb_buffer + from,
				     vtb2->vtb_buffer + vtb2->vtb_tail,
				     len*sizeof(uint16_t));
		} else {
			bcopy(vtb1->vtb_buffer + from,
			      vtb2->vtb_buffer + vtb2->vtb_tail,
			      len*sizeof(uint16_t));
		}
		from += len;
		count -= len;
		vtb2->vtb_tail = vtb_wrap(vtb2, vtb2->vtb_tail, len);
	}
}

void
sc_vtb_seek(sc_vtb_t *vtb, int pos)
{
	vtb->vtb_tail = pos%vtb->vtb_size;
}

void
sc_vtb_erase(sc_vtb_t *vtb, int at, int count, int c, int attr)
{
	if (at + count > vtb->vtb_size)
		count = vtb->vtb_size - at;
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		fillw_io(attr | c, vtb->vtb_buffer + at, count);
	else
		fillw(attr | c, vtb->vtb_buffer + at, count);
}

void
sc_vtb_move(sc_vtb_t *vtb, int from, int to, int count)
{
	if (from + count > vtb->vtb_size)
		count = vtb->vtb_size - from;
	if (to + count > vtb->vtb_size)
		count = vtb->vtb_size - to;
	if (count <= 0)
		return;
	if (vtb->vtb_type == VTB_FRAMEBUFFER) {
		bcopy_io(vtb->vtb_buffer + from, vtb->vtb_buffer + to,
			 count*sizeof(uint16_t)); 
	} else {
		bcopy(vtb->vtb_buffer + from, vtb->vtb_buffer + to,
		      count*sizeof(uint16_t));
	}
}

void
sc_vtb_delete(sc_vtb_t *vtb, int at, int count, int c, int attr)
{
	int len;

	if (at + count > vtb->vtb_size)
		count = vtb->vtb_size - at;
	len = vtb->vtb_size - at - count;
	if (len > 0) {
		if (vtb->vtb_type == VTB_FRAMEBUFFER) {
			bcopy_io(vtb->vtb_buffer + at + count,
				 vtb->vtb_buffer + at,
				 len*sizeof(uint16_t)); 
		} else {
			bcopy(vtb->vtb_buffer + at + count,
			      vtb->vtb_buffer + at,
			      len*sizeof(uint16_t)); 
		}
	}
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		fillw_io(attr | c, vtb->vtb_buffer + at + len,
			 vtb->vtb_size - at - len);
	else
		fillw(attr | c, vtb->vtb_buffer + at + len,
		      vtb->vtb_size - at - len);
}

void
sc_vtb_ins(sc_vtb_t *vtb, int at, int count, int c, int attr)
{
	if (at + count > vtb->vtb_size) {
		count = vtb->vtb_size - at;
	} else {
		if (vtb->vtb_type == VTB_FRAMEBUFFER) {
			bcopy_io(vtb->vtb_buffer + at,
				 vtb->vtb_buffer + at + count,
				 (vtb->vtb_size - at - count)*sizeof(uint16_t));
		} else {
			bcopy(vtb->vtb_buffer + at,
			      vtb->vtb_buffer + at + count,
			      (vtb->vtb_size - at - count)*sizeof(uint16_t)); 
		}
	}
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		fillw_io(attr | c, vtb->vtb_buffer + at, count);
	else
		fillw(attr | c, vtb->vtb_buffer + at, count);
}
