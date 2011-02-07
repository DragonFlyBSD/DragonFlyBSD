/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * System resource registration, reference counter, and allocation
 * management subsystem.
 *
 * All major system resources use this structure and API to associate
 * a machine-unique sysref with a major system resource structure, to
 * reference count that structure, to register the structure and provide
 * a dictionary lookup feature for remote access, and to provide an
 * allocation and release backend.
 */

#ifndef _SYS_SYSREF2_H_
#define _SYS_SYSREF2_H_

#ifndef _SYS_SYSREF_H_
#include <sys/sysref.h>
#endif
#include <machine/atomic.h>

void _sysref_put(struct sysref *);

/*
 * Normally only 1+ transitions can occur.  Note that a negative ref count
 * is used as a marker to indicate that a structure is undergoing deletion
 * and that temporary refs during the process of deletion may occur.
 *
 * The get interface is the primary reference counting interface.
 */
static __inline
void
sysref_get(struct sysref *sr)
{
	KKASSERT((sr->flags & SRF_PUTAWAY) == 0);
	atomic_add_int(&sr->refcnt, 1);
}

/*
 * 1->0 transitions, and transitions occuring with negative ref counts,
 * require special handling.  All other transitions can be done with a
 * trivial locked bus cycle.
 */
static __inline
void
sysref_put(struct sysref *sr)
{
	int count = sr->refcnt;

	KKASSERT((sr->flags & SRF_PUTAWAY) == 0);
	if (count <= 1 || atomic_cmpset_int(&sr->refcnt, count, count - 1) == 0)
		_sysref_put(sr);
}

/*
 * Return true of the object is fully active
 */
static __inline
int
sysref_isactive(struct sysref *sr)
{
	return(sr->refcnt > 0);
}

/*
 * Return true of the object is being deactivated
 */
static __inline
int
sysref_isinactive(struct sysref *sr)
{
	return(sr->refcnt <= 0);
}

/*
 * Return true if we control the last deactivation ref on the object
 */
static __inline
int
sysref_islastdeactivation(struct sysref *sr)
{
	return(sr->refcnt == -0x40000000);
}

#ifdef _KERNEL

void sysref_init(struct sysref *sr, struct sysref_class *);
void *sysref_alloc(struct sysref_class *class);
void sysref_activate(struct sysref *);

#endif

#endif
