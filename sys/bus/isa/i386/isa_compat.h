/*-
 * Copyright (c) 1998 Doug Rabson
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
 * $FreeBSD: src/sys/i386/isa/isa_compat.h,v 1.27.2.11 2002/10/05 18:31:48 scottl Exp $
 */

#include "use_cy.h"
#include "use_stl.h"
#include "use_stli.h"

struct old_isa_driver {
	int			type;
	struct isa_driver	*driver;
};

extern struct isa_driver cydriver;
extern struct isa_driver stldriver;
extern struct isa_driver stlidriver;


static struct old_isa_driver old_drivers[] = {

/* Sensitive TTY */

/* Sensitive BIO */

/* Sensitive NET */

/* Sensitive CAM */

/* TTY */

#if NCY > 0
	{ 0, &cydriver },
#endif
#if NSTL > 0
	{ 0, &stldriver },
#endif
#if NSTLI > 0
	{ 0, &stlidriver },
#endif

/* BIO */

/* NET */

/* MISC */

};

#define old_drivers_count NELEM(old_drivers)

