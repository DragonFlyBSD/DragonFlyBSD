/*-
 * Copyright (c) 2000 Peter Wemm <peter@FreeBSD.org>
 * Copyright (c) 2008 The DragonFly Project.
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
 * $FreeBSD: src/sys/i386/include/nexusvar.h,v 1.1 2000/09/28 00:37:31 peter Exp $
 * $DragonFly: src/sys/platform/pc64/include/nexusvar.h,v 1.1 2008/08/29 17:07:17 dillon Exp $
 */

#ifndef _MACHINE_NEXUSVAR_H_
#define	_MACHINE_NEXUSVAR_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_CONF_H_
#include <sys/bus.h>
#endif

enum nexus_device_ivars {
	NEXUS_IVAR_PCIBUS
};

#define NEXUS_ACCESSOR(A, B, T)						  \
									  \
static __inline T nexus_get_ ## A(device_t dev)				  \
{									  \
	uintptr_t v;							  \
	BUS_READ_IVAR(device_get_parent(dev), dev, NEXUS_IVAR_ ## B, &v); \
	return (T) v;							  \
}									  \
									  \
static __inline void nexus_set_ ## A(device_t dev, T t)			  \
{									  \
	uintptr_t v = (uintptr_t) t;					  \
	BUS_WRITE_IVAR(device_get_parent(dev), dev, NEXUS_IVAR_ ## B, v); \
}

#ifdef _KERNEL
NEXUS_ACCESSOR(pcibus,			PCIBUS,		u_int32_t)
#endif

#undef NEXUS_ACCESSOR

#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#endif	/* !_MACHINE_NEXUSVAR_H_ */
