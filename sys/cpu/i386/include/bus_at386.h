/*	$NetBSD: bus.h,v 1.12 1997/10/01 08:25:15 fvdl Exp $	*/

/*-
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1996 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 * $FreeBSD: src/sys/i386/include/bus_at386.h,v 1.8.2.3 2002/03/03 05:42:50 nyan Exp $
 * $DragonFly: src/sys/cpu/i386/include/bus_at386.h,v 1.10 2006/11/07 06:43:22 dillon Exp $
 */

#ifndef _CPU_BUS_AT386_H_
#define _CPU_BUS_AT386_H_

#include <machine/cpufunc.h>

/*
 * Values for the i386 bus space tag, not to be used directly by MI code.
 */
#define	I386_BUS_SPACE_IO	0	/* space is i/o space */
#define I386_BUS_SPACE_MEM	1	/* space is mem space */

/*
 * Bus address and size types
 */
typedef u_int32_t bus_addr_t;
typedef u_int32_t bus_size_t;

#define BUS_SPACE_MAXSIZE_24BIT	0xFFFFFF
#define BUS_SPACE_MAXSIZE_32BIT 0xFFFFFFFF
#define BUS_SPACE_MAXSIZE	(64 * 1024) /* Maximum supported size */
#define BUS_SPACE_MAXADDR_24BIT	0xFFFFFF
#define BUS_SPACE_MAXADDR_32BIT 0xFFFFFFFF
#define BUS_SPACE_MAXADDR	BUS_SPACE_MAXADDR_32BIT

#define BUS_SPACE_UNRESTRICTED	(~0)

/*
 * Access methods for bus resources and address space.
 */
typedef	int bus_space_tag_t;
typedef	u_int bus_space_handle_t;

/*
 * Map a region of device bus space into CPU virtual address space.
 */

#define	BUS_SPACE_MAP_CACHEABLE		0x01
#define	BUS_SPACE_MAP_LINEAR		0x02

int	bus_space_map(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
		      int flags, bus_space_handle_t *bshp);

/*
 * Unmap a region of device bus space.
 */

static __inline void bus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh,
				     bus_size_t size);

static __inline void
bus_space_unmap(bus_space_tag_t t __unused, bus_space_handle_t bsh __unused, bus_size_t size __unused )
{
}

/*
 * Get a new handle for a subregion of an already-mapped area of bus space.
 */

static __inline int bus_space_subregion(bus_space_tag_t t,
					bus_space_handle_t bsh,
					bus_size_t offset, bus_size_t size,
					bus_space_handle_t *nbshp);

static __inline int
bus_space_subregion(bus_space_tag_t t __unused, bus_space_handle_t bsh,
		    bus_size_t offset, bus_size_t size __unused,
		    bus_space_handle_t *nbshp)
{

	*nbshp = bsh + offset;
	return (0);
}

static __inline void *
bus_space_kva(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t offset)
{
	if (tag == I386_BUS_SPACE_IO)
		return ((void *)0);
	return ((void *)(handle + offset));
}

/*
 * Allocate a region of memory that is accessible to devices in bus space.
 */

int	bus_space_alloc(bus_space_tag_t t, bus_addr_t rstart,
			bus_addr_t rend, bus_size_t size, bus_size_t align,
			bus_size_t boundary, int flags, bus_addr_t *addrp,
			bus_space_handle_t *bshp);

/*
 * Free a region of bus space accessible memory.
 */

static __inline void bus_space_free(bus_space_tag_t t, bus_space_handle_t bsh,
				    bus_size_t size);

static __inline void
bus_space_free(bus_space_tag_t t __unused, bus_space_handle_t bsh __unused, bus_size_t size __unused)
{
}

/*
 * Read a 1, 2, 4, or 8 byte quantity from bus space
 * described by tag/handle/offset.
 */
static __inline u_int8_t bus_space_read_1(bus_space_tag_t tag,
					  bus_space_handle_t handle,
					  bus_size_t offset);

static __inline u_int16_t bus_space_read_2(bus_space_tag_t tag,
					   bus_space_handle_t handle,
					   bus_size_t offset);

static __inline u_int32_t bus_space_read_4(bus_space_tag_t tag,
					   bus_space_handle_t handle,
					   bus_size_t offset);

static __inline u_int8_t
bus_space_read_1(bus_space_tag_t tag, bus_space_handle_t handle,
		 bus_size_t offset)
{
	if (tag == I386_BUS_SPACE_IO)
		return (inb(handle + offset));
	return (*(volatile u_int8_t *)(handle + offset));
}

static __inline u_int16_t
bus_space_read_2(bus_space_tag_t tag, bus_space_handle_t handle,
		 bus_size_t offset)
{
	if (tag == I386_BUS_SPACE_IO)
		return (inw(handle + offset));
	return (*(volatile u_int16_t *)(handle + offset));
}

static __inline u_int32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
		 bus_size_t offset)
{
	if (tag == I386_BUS_SPACE_IO)
		return (inl(handle + offset));
	return (*(volatile u_int32_t *)(handle + offset));
}

/*
 * Read `count' 1, 2, 4, or 8 byte quantities from bus space
 * described by tag/handle/offset and copy into buffer provided.
 */
static __inline void bus_space_read_multi_1(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int8_t *addr,
					    size_t count);

static __inline void bus_space_read_multi_2(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int16_t *addr,
					    size_t count);

static __inline void bus_space_read_multi_4(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int32_t *addr,
					    size_t count);

static __inline void
bus_space_read_multi_1(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int8_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		insb(bsh + offset, addr, count);
	} else {
		__asm __volatile("				\n\
			cld					\n\
		1:	movb (%2),%%al				\n\
			stosb					\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory");
	}
}

static __inline void
bus_space_read_multi_2(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int16_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		insw(bsh + offset, addr, count);
	} else {
		__asm __volatile("				\n\
			cld					\n\
		1:	movw (%2),%%ax				\n\
			stosw					\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory");
	}
}

static __inline void
bus_space_read_multi_4(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int32_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		insl(bsh + offset, addr, count);
	} else {
		__asm __volatile("				\n\
			cld					\n\
		1:	movl (%2),%%eax				\n\
			stosl					\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory");
	}
}

/*
 * Read `count' 1, 2, 4, or 8 byte quantities from bus space
 * described by tag/handle and starting at `offset' and copy into
 * buffer provided.
 */
static __inline void bus_space_read_region_1(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset, u_int8_t *addr,
					     size_t count);

static __inline void bus_space_read_region_2(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset, u_int16_t *addr,
					     size_t count);

static __inline void bus_space_read_region_4(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset, u_int32_t *addr,
					     size_t count);


static __inline void
bus_space_read_region_1(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, u_int8_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
		1:	inb %w2,%%al				\n\
			stosb					\n\
			incl %2					\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count), "=d" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "%eax", "memory", "cc");
	} else {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsb"					:
		    "=D" (addr), "=c" (count), "=S" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "memory", "cc");
	}
}

static __inline void
bus_space_read_region_2(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, u_int16_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
		1:	inw %w2,%%ax				\n\
			stosw					\n\
			addl $2,%2				\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count), "=d" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "%eax", "memory", "cc");
	} else {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsw"					:
		    "=D" (addr), "=c" (count), "=S" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "memory", "cc");
	}
}

static __inline void
bus_space_read_region_4(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, u_int32_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
		1:	inl %w2,%%eax				\n\
			stosl					\n\
			addl $4,%2				\n\
			loop 1b"				:
		    "=D" (addr), "=c" (count), "=d" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "%eax", "memory", "cc");
	} else {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsl"					:
		    "=D" (addr), "=c" (count), "=S" (_port_)	:
		    "0" (addr), "1" (count), "2" (_port_)	:
		    "memory", "cc");
	}
}

/*
 * Write the 1, 2, 4, or 8 byte value `value' to bus space
 * described by tag/handle/offset.
 */

static __inline void bus_space_write_1(bus_space_tag_t tag,
				       bus_space_handle_t bsh,
				       bus_size_t offset, u_int8_t value);

static __inline void bus_space_write_2(bus_space_tag_t tag,
				       bus_space_handle_t bsh,
				       bus_size_t offset, u_int16_t value);

static __inline void bus_space_write_4(bus_space_tag_t tag,
				       bus_space_handle_t bsh,
				       bus_size_t offset, u_int32_t value);

static __inline void
bus_space_write_1(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int8_t value)
{
	if (tag == I386_BUS_SPACE_IO)
		outb(bsh + offset, value);
	else
		*(volatile u_int8_t *)(bsh + offset) = value;
}

static __inline void
bus_space_write_2(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int16_t value)
{
	if (tag == I386_BUS_SPACE_IO)
		outw(bsh + offset, value);
	else
		*(volatile u_int16_t *)(bsh + offset) = value;
}

static __inline void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int32_t value)
{
	if (tag == I386_BUS_SPACE_IO)
		outl(bsh + offset, value);
	else
		*(volatile u_int32_t *)(bsh + offset) = value;
}

/*
 * Write `count' 1, 2, 4, or 8 byte quantities from the buffer
 * provided to bus space described by tag/handle/offset.
 */

static __inline void bus_space_write_multi_1(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset,
					     const u_int8_t *addr,
					     size_t count);
static __inline void bus_space_write_multi_2(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset,
					     const u_int16_t *addr,
					     size_t count);

static __inline void bus_space_write_multi_4(bus_space_tag_t tag,
					     bus_space_handle_t bsh,
					     bus_size_t offset,
					     const u_int32_t *addr,
					     size_t count);

static __inline void
bus_space_write_multi_1(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, const u_int8_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		outsb(bsh + offset, addr, count);
	} else {
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsb					\n\
			movb %%al,(%2)				\n\
			loop 1b"				:
		    "=S" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory", "cc");
	}
}

static __inline void
bus_space_write_multi_2(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, const u_int16_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		outsw(bsh + offset, addr, count);
	} else {
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsw					\n\
			movw %%ax,(%2)				\n\
			loop 1b"				:
		    "=S" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory", "cc");
	}
}

static __inline void
bus_space_write_multi_4(bus_space_tag_t tag, bus_space_handle_t bsh,
			bus_size_t offset, const u_int32_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		outsl(bsh + offset, addr, count);
	} else {
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsl					\n\
			movl %%eax,(%2)				\n\
			loop 1b"				:
		    "=S" (addr), "=c" (count)			:
		    "r" (bsh + offset), "0" (addr), "1" (count)	:
		    "%eax", "memory", "cc");
	}
}

/*
 * Write `count' 1, 2, 4, or 8 byte quantities from the buffer provided
 * to bus space described by tag/handle starting at `offset'.
 */

static __inline void bus_space_write_region_1(bus_space_tag_t tag,
					      bus_space_handle_t bsh,
					      bus_size_t offset,
					      const u_int8_t *addr,
					      size_t count);
static __inline void bus_space_write_region_2(bus_space_tag_t tag,
					      bus_space_handle_t bsh,
					      bus_size_t offset,
					      const u_int16_t *addr,
					      size_t count);
static __inline void bus_space_write_region_4(bus_space_tag_t tag,
					      bus_space_handle_t bsh,
					      bus_size_t offset,
					      const u_int32_t *addr,
					      size_t count);

static __inline void
bus_space_write_region_1(bus_space_tag_t tag, bus_space_handle_t bsh,
			 bus_size_t offset, const u_int8_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsb					\n\
			outb %%al,%w0				\n\
			incl %0					\n\
			loop 1b"				:
		    "=d" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "%eax", "memory", "cc");
	} else {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsb"					:
		    "=D" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "memory", "cc");
	}
}

static __inline void
bus_space_write_region_2(bus_space_tag_t tag, bus_space_handle_t bsh,
			 bus_size_t offset, const u_int16_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsw					\n\
			outw %%ax,%w0				\n\
			addl $2,%0				\n\
			loop 1b"				:
		    "=d" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "%eax", "memory", "cc");
	} else {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsw"					:
		    "=D" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "memory", "cc");
	}
}

static __inline void
bus_space_write_region_4(bus_space_tag_t tag, bus_space_handle_t bsh,
			 bus_size_t offset, const u_int32_t *addr, size_t count)
{
	if (tag == I386_BUS_SPACE_IO) {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
		1:	lodsl					\n\
			outl %%eax,%w0				\n\
			addl $4,%0				\n\
			loop 1b"				:
		    "=d" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "%eax", "memory", "cc");
	} else {
		int _port_ = bsh + offset;			\
		__asm __volatile("				\n\
			cld					\n\
			repne					\n\
			movsl"					:
		    "=D" (_port_), "=S" (addr), "=c" (count)	:
		    "0" (_port_), "1" (addr), "2" (count)	:
		    "memory", "cc");
	}
}

/*
 * Write the 1, 2, 4, or 8 byte value `val' to bus space described
 * by tag/handle/offset `count' times.
 */

static __inline void bus_space_set_multi_1(bus_space_tag_t tag,
					   bus_space_handle_t bsh,
					   bus_size_t offset,
					   u_int8_t value, size_t count);
static __inline void bus_space_set_multi_2(bus_space_tag_t tag,
					   bus_space_handle_t bsh,
					   bus_size_t offset,
					   u_int16_t value, size_t count);
static __inline void bus_space_set_multi_4(bus_space_tag_t tag,
					   bus_space_handle_t bsh,
					   bus_size_t offset,
					   u_int32_t value, size_t count);

static __inline void
bus_space_set_multi_1(bus_space_tag_t tag, bus_space_handle_t bsh,
		      bus_size_t offset, u_int8_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == I386_BUS_SPACE_IO) {
		while (count--)
			outb(addr, value);
	} else {
		while (count--)
			*(volatile u_int8_t *)(addr) = value;
	}
}

static __inline void
bus_space_set_multi_2(bus_space_tag_t tag, bus_space_handle_t bsh,
		     bus_size_t offset, u_int16_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == I386_BUS_SPACE_IO) {
		while (count--)
			outw(addr, value);
	} else {
		while (count--)
			*(volatile u_int16_t *)(addr) = value;
	}
}

static __inline void
bus_space_set_multi_4(bus_space_tag_t tag, bus_space_handle_t bsh,
		      bus_size_t offset, u_int32_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == I386_BUS_SPACE_IO) {
		while (count--)
			outl(addr, value);
	} else {
		while (count--)
			*(volatile u_int32_t *)(addr) = value;
	}
}

/*
 * Write `count' 1, 2, 4, or 8 byte value `val' to bus space described
 * by tag/handle starting at `offset'.
 */

static __inline void bus_space_set_region_1(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int8_t value,
					    size_t count);
static __inline void bus_space_set_region_2(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int16_t value,
					    size_t count);
static __inline void bus_space_set_region_4(bus_space_tag_t tag,
					    bus_space_handle_t bsh,
					    bus_size_t offset, u_int32_t value,
					    size_t count);

static __inline void
bus_space_set_region_1(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int8_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == I386_BUS_SPACE_IO) {
		for (; count != 0; count--, addr++)
			outb(addr, value);
	} else {
		for (; count != 0; count--, addr++)
			*(volatile u_int8_t *)(addr) = value;
	}
}

static __inline void
bus_space_set_region_2(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int16_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == I386_BUS_SPACE_IO) {
		for (; count != 0; count--, addr += 2)
			outw(addr, value);
	} else {
		for (; count != 0; count--, addr += 2)
			*(volatile u_int16_t *)(addr) = value;
	}
}

static __inline void
bus_space_set_region_4(bus_space_tag_t tag, bus_space_handle_t bsh,
		       bus_size_t offset, u_int32_t value, size_t count)
{
	bus_space_handle_t addr = bsh + offset;

	if (tag == I386_BUS_SPACE_IO) {
		for (; count != 0; count--, addr += 4)
			outl(addr, value);
	} else {
		for (; count != 0; count--, addr += 4)
			*(volatile u_int32_t *)(addr) = value;
	}
}

/*
 * Copy `count' 1, 2, 4, or 8 byte values from bus space starting
 * at tag/bsh1/off1 to bus space starting at tag/bsh2/off2.
 */

static __inline void bus_space_copy_region_1(bus_space_tag_t tag,
					     bus_space_handle_t bsh1,
					     bus_size_t off1,
					     bus_space_handle_t bsh2,
					     bus_size_t off2, size_t count);

static __inline void bus_space_copy_region_2(bus_space_tag_t tag,
					     bus_space_handle_t bsh1,
					     bus_size_t off1,
					     bus_space_handle_t bsh2,
					     bus_size_t off2, size_t count);

static __inline void bus_space_copy_region_4(bus_space_tag_t tag,
					     bus_space_handle_t bsh1,
					     bus_size_t off1,
					     bus_space_handle_t bsh2,
					     bus_size_t off2, size_t count);

static __inline void
bus_space_copy_region_1(bus_space_tag_t tag, bus_space_handle_t bsh1,
			bus_size_t off1, bus_space_handle_t bsh2,
			bus_size_t off2, size_t count)
{
	bus_space_handle_t addr1 = bsh1 + off1;
	bus_space_handle_t addr2 = bsh2 + off2;

	if (tag == I386_BUS_SPACE_IO) {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1++, addr2++)
				outb(addr2, inb(addr1));
		} else {
			/* dest after src: copy backwards */
			for (addr1 += (count - 1), addr2 += (count - 1);
			    count != 0; count--, addr1--, addr2--)
				outb(addr2, inb(addr1));
		}
	} else {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1++, addr2++)
				*(volatile u_int8_t *)(addr2) =
				    *(volatile u_int8_t *)(addr1);
		} else {
			/* dest after src: copy backwards */
			for (addr1 += (count - 1), addr2 += (count - 1);
			    count != 0; count--, addr1--, addr2--)
				*(volatile u_int8_t *)(addr2) =
				    *(volatile u_int8_t *)(addr1);
		}
	}
}

static __inline void
bus_space_copy_region_2(bus_space_tag_t tag, bus_space_handle_t bsh1,
			bus_size_t off1, bus_space_handle_t bsh2,
			bus_size_t off2, size_t count)
{
	bus_space_handle_t addr1 = bsh1 + off1;
	bus_space_handle_t addr2 = bsh2 + off2;

	if (tag == I386_BUS_SPACE_IO) {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1 += 2, addr2 += 2)
				outw(addr2, inw(addr1));
		} else {
			/* dest after src: copy backwards */
			for (addr1 += 2 * (count - 1), addr2 += 2 * (count - 1);
			    count != 0; count--, addr1 -= 2, addr2 -= 2)
				outw(addr2, inw(addr1));
		}
	} else {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1 += 2, addr2 += 2)
				*(volatile u_int16_t *)(addr2) =
				    *(volatile u_int16_t *)(addr1);
		} else {
			/* dest after src: copy backwards */
			for (addr1 += 2 * (count - 1), addr2 += 2 * (count - 1);
			    count != 0; count--, addr1 -= 2, addr2 -= 2)
				*(volatile u_int16_t *)(addr2) =
				    *(volatile u_int16_t *)(addr1);
		}
	}
}

static __inline void
bus_space_copy_region_4(bus_space_tag_t tag, bus_space_handle_t bsh1,
			bus_size_t off1, bus_space_handle_t bsh2,
			bus_size_t off2, size_t count)
{
	bus_space_handle_t addr1 = bsh1 + off1;
	bus_space_handle_t addr2 = bsh2 + off2;

	if (tag == I386_BUS_SPACE_IO) {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1 += 4, addr2 += 4)
				outl(addr2, inl(addr1));
		} else {
			/* dest after src: copy backwards */
			for (addr1 += 4 * (count - 1), addr2 += 4 * (count - 1);
			    count != 0; count--, addr1 -= 4, addr2 -= 4)
				outl(addr2, inl(addr1));
		}
	} else {
		if (addr1 >= addr2) {
			/* src after dest: copy forward */
			for (; count != 0; count--, addr1 += 4, addr2 += 4)
				*(volatile u_int32_t *)(addr2) =
				    *(volatile u_int32_t *)(addr1);
		} else {
			/* dest after src: copy backwards */
			for (addr1 += 4 * (count - 1), addr2 += 4 * (count - 1);
			    count != 0; count--, addr1 -= 4, addr2 -= 4)
				*(volatile u_int32_t *)(addr2) =
				    *(volatile u_int32_t *)(addr1);
		}
	}
}

/*
 * Bus read/write barrier methods.
 *
 *	void bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t bsh,
 *			       bus_size_t offset, bus_size_t len, int flags);
 *
 * Note that BUS_SPACE_BARRIER_WRITE doesn't do anything other than
 * prevent reordering by the compiler; all Intel x86 processors currently
 * retire operations outside the CPU in program order.
 */
#define	BUS_SPACE_BARRIER_READ	0x01		/* force read barrier */
#define	BUS_SPACE_BARRIER_WRITE	0x02		/* force write barrier */

static __inline void
bus_space_barrier(bus_space_tag_t tag __unused, bus_space_handle_t bsh __unused,
		bus_size_t offset __unused, bus_size_t len __unused, int flags)
{
	if (flags & BUS_SPACE_BARRIER_READ)
		__asm __volatile("lock; addl $0,0(%%esp)" : : : "memory");
	else
		__asm __volatile("" : : : "memory");
}

#endif /* _CPU_BUS_AT386_H_ */
