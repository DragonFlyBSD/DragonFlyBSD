/*-
 * Copyright (c) 2000 Mitsaru Iwasaki
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
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
 *
 * $FreeBSD: src/sys/dev/acpica/Osd/OsdMemory.c,v 1.11 2004/04/14 03:39:08 njl Exp $
 */

/*
 * 6.2 : Memory Management
 */

#include "acpi.h"

#include <sys/kernel.h>
#include <sys/malloc.h>
#include <vm/vm.h>
#include <vm/pmap.h>

MALLOC_DEFINE(M_ACPICA, "acpica", "ACPICA memory pool");

struct acpi_memtrack {
    struct acpi_memtrack *next;
    void *base;
    ACPI_SIZE size;
#ifdef ACPI_DEBUG_MEMMAP
    int freed;
    struct {
	const char *func;
	int line;
    } mapper, unmapper;
#endif
};

typedef struct acpi_memtrack *acpi_memtrack_t;

static acpi_memtrack_t acpi_mapbase;

void *
AcpiOsAllocate(ACPI_SIZE Size)
{
    return (kmalloc(Size, M_ACPICA, M_INTWAIT));
}

void
AcpiOsFree(void *Memory)
{
    kfree(Memory, M_ACPICA);
}

#ifdef ACPI_DEBUG_MEMMAP
void *
_AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS Where, ACPI_SIZE Length,
		 const char *caller, int line)
#else
void *
AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS Where, ACPI_SIZE Length)
#endif
{
    acpi_memtrack_t track;
    void *map;

    map = pmap_mapdev((vm_offset_t)Where, Length);
    if (map == NULL)
	return(NULL);
    else {
#ifdef ACPI_DEBUG_MEMMAP
	for (track = acpi_mapbase; track != NULL; track = track->next) {
	    if (track->base == map)
		break;
	}
#else
	track = NULL;
#endif
	if (track == NULL) {
	    track = kmalloc(sizeof(*track), M_ACPICA, M_INTWAIT);
	    track->next = acpi_mapbase;
	    track->base = map;
	}
	track->size = Length;
#ifdef ACPI_DEBUG_MEMMAP
	track->freed = 0;
	track->mapper.func = caller;
	track->mapper.line = line;
	track->unmapper.func = "";
	track->unmapper.line = 0;
#endif
	acpi_mapbase = track;
    }
    return(map);
}

#ifdef ACPI_DEBUG_MEMMAP
void
_AcpiOsUnmapMemory(void *LogicalAddress, ACPI_SIZE Length,
		   const char *caller, int line)
#else
void
AcpiOsUnmapMemory(void *LogicalAddress, ACPI_SIZE Length)
#endif
{
    struct acpi_memtrack **ptrack;
    acpi_memtrack_t track;

again:
    for (ptrack = &acpi_mapbase; (track = *ptrack); ptrack = &track->next) {
#ifdef ACPI_DEBUG_MEMMAP
	if (track->freed)
	    continue;
#endif
	/*
	 * Exact match, degenerate case
	 */
	if (track->base == LogicalAddress && track->size == Length) {
	    *ptrack = track->next;
	    pmap_unmapdev((vm_offset_t)track->base, track->size);
#ifdef ACPI_DEBUG_MEMMAP
	    track->freed = 1;
	    track->unmapper.func = caller;
	    track->unmapper.line = line;
#else
	    kfree(track, M_ACPICA);
#endif
	    return;
	}
	/*
	 * Completely covered
	 */
	if ((char *)LogicalAddress <= (char *)track->base &&
	    (char *)LogicalAddress + Length >= (char *)track->base + track->size
	) {
	    *ptrack = track->next;
	    pmap_unmapdev((vm_offset_t)track->base, track->size);
	    kprintf("AcpiOsUnmapMemory: Warning, deallocation request too"
		   " large! %p/%08jx (actual was %p/%08jx)\n",
		   LogicalAddress, (intmax_t)Length,
		   track->base, (intmax_t)track->size);
#ifdef ACPI_DEBUG_MEMMAP
	    track->freed = 1;
	    track->unmapper.func = caller;
	    track->unmapper.line = line;
#else
	    kfree(track, M_ACPICA);
#endif
	    goto again;
	}

	/*
	 * Overlapping
	 */
	if ((char *)LogicalAddress + Length >= (char *)track->base &&
	    (char *)LogicalAddress < (char *)track->base + track->size
	) {
	    kprintf("AcpiOsUnmapMemory: Warning, deallocation did not "
		   "track allocation: %p/%08jx (actual was %p/%08jx)\n",
		   LogicalAddress, (intmax_t)Length,
		   track->base, (intmax_t)track->size);
	}
    }
    kprintf("AcpiOsUnmapMemory: Warning, broken ACPI, bad unmap: %p/%08jx\n",
	    LogicalAddress, (intmax_t)Length);
#ifdef ACPI_DEBUG_MEMMAP
    for (track = acpi_mapbase; track != NULL; track = track->next) {
	if (track->freed && track->base == LogicalAddress) {
	    kprintf("%s: unmapping: %p/%0*jx, mapped by %s:%d,"
		   "last unmapped by %s:%d\n",
		__func__, LogicalAddress, sizeof(Length) * 2, (uintmax_t)Length,
		track->mapper.func, track->mapper.line,
		track->unmapper.func, track->unmapper.line
	    );
	}
    }
#endif
}

ACPI_STATUS
AcpiOsGetPhysicalAddress(void *LogicalAddress,
    ACPI_PHYSICAL_ADDRESS *PhysicalAddress)
{
    /* We can't necessarily do this, so cop out. */
    return (AE_BAD_ADDRESS);
}

/*
 * There is no clean way to do this.  We make the charitable assumption
 * that callers will not pass garbage to us.
 */
BOOLEAN
AcpiOsReadable (void *Pointer, ACPI_SIZE Length)
{
    return (TRUE);
}

BOOLEAN
AcpiOsWritable (void *Pointer, ACPI_SIZE Length)
{
    return (TRUE);
}

ACPI_STATUS
AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 *Value, UINT32 Width)
{
    void	*LogicalAddress;

    LogicalAddress = AcpiOsMapMemory(Address, Width / 8);
    if (LogicalAddress == NULL)
	return (AE_NOT_EXIST);

    switch (Width) {
    case 8:
	*(u_int8_t *)Value = (*(volatile u_int8_t *)LogicalAddress);
	break;
    case 16:
	*(u_int16_t *)Value = (*(volatile u_int16_t *)LogicalAddress);
	break;
    case 32:
	*(u_int32_t *)Value = (*(volatile u_int32_t *)LogicalAddress);
	break;
    case 64:
	*(u_int64_t *)Value = (*(volatile u_int64_t *)LogicalAddress);
	break;
    default:
	/* debug trap goes here */
	break;
    }

    AcpiOsUnmapMemory(LogicalAddress, Width / 8);

    return (AE_OK);
}

ACPI_STATUS
AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 Value, UINT32 Width)
{
    void	*LogicalAddress;

    LogicalAddress = AcpiOsMapMemory(Address, Width / 8);
    if (LogicalAddress == NULL)
	return (AE_NOT_EXIST);

    switch (Width) {
    case 8:
	(*(volatile u_int8_t *)LogicalAddress) = Value & 0xff;
	break;
    case 16:
	(*(volatile u_int16_t *)LogicalAddress) = Value & 0xffff;
	break;
    case 32:
	(*(volatile u_int32_t *)LogicalAddress) = Value & 0xffffffff;
	break;
    case 64:
	(*(volatile u_int64_t *)LogicalAddress) = Value;
	break;
    default:
	/* debug trap goes here */
	break;
    }

    AcpiOsUnmapMemory(LogicalAddress, Width / 8);

    return (AE_OK);
}
