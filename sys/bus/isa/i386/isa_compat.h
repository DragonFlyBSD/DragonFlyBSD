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
 * $DragonFly: src/sys/bus/isa/i386/isa_compat.h,v 1.10 2005/10/12 17:35:47 dillon Exp $
 */

#include "use_vt.h"
#include "use_cx.h"
#include "use_el.h"
#include "use_le.h"
#include "use_rdp.h"
#include "use_pcm.h"
#include "use_mcd.h"
#include "use_scd.h"
#include "use_wt.h"
#include "use_ctx.h"
#include "use_spigot.h"
#include "use_gp.h"
#include "use_gsc.h"
#include "use_cy.h"
#include "use_dgb.h"
#include "use_labpc.h"
#include "use_rc.h"
#include "use_tw.h"
#include "use_asc.h"
#include "use_stl.h"
#include "use_stli.h"
#include "use_fla.h"

struct old_isa_driver {
	int			type;
	struct isa_driver	*driver;
};

extern struct isa_driver  vtdriver;
extern struct isa_driver  cxdriver;
extern struct isa_driver  eldriver;
extern struct isa_driver  ledriver;
extern struct isa_driver rdpdriver;
extern struct isa_driver mcddriver;
extern struct isa_driver scddriver;
extern struct isa_driver  wtdriver;
extern struct isa_driver ctxdriver;
extern struct isa_driver spigotdriver;
extern struct isa_driver  gpdriver;
extern struct isa_driver gscdriver;
extern struct isa_driver  cydriver;
extern struct isa_driver dgbdriver;
extern struct isa_driver labpcdriver;
extern struct isa_driver  rcdriver;
extern struct isa_driver  rpdriver;
extern struct isa_driver  twdriver;
extern struct isa_driver ascdriver;
extern struct isa_driver stldriver;
extern struct isa_driver stlidriver;
extern struct isa_driver lorandriver;


static struct old_isa_driver old_drivers[] = {

/* Sensitive TTY */

/* Sensitive BIO */

/* Sensitive NET */
#if NRDP > 0
	{ 0, &rdpdriver },
#endif

/* Sensitive CAM */

/* TTY */

#if NVT > 0
	{ 0, &vtdriver },
#endif
#if NGP > 0
	{ 0, &gpdriver },
#endif
#if NGSC > 0
	{ 0, &gscdriver },
#endif
#if NCY > 0
	{ INTR_FAST, &cydriver },
#endif
#if NDGB > 0
	{ 0, &dgbdriver },
#endif
#if NLABPC > 0
	{ 0, &labpcdriver },
#endif
#if NRC > 0
	{ 0, &rcdriver },
#endif
#if NRP > 0
	{ 0, &rpdriver },
#endif
#if NTW > 0
	{ 0, &twdriver },
#endif
#if NASC > 0
	{ 0, &ascdriver },
#endif
#if NSTL > 0
	{ 0, &stldriver },
#endif
#if NSTLI > 0
	{ 0, &stlidriver },
#endif
#if NLORAN > 0
	{ INTR_FAST, &lorandriver },
#endif

/* BIO */

#if NMCD > 0
	{ 0, &mcddriver },
#endif
#if NSCD > 0
	{ 0, &scddriver },
#endif
#if NWT > 0
	{ 0, &wtdriver },
#endif

/* NET */

#if NLE > 0
	{ 0, &ledriver },
#endif
#if NCX > 0
	{ 0, &cxdriver },
#endif
#if NEL > 0
	{ 0, &eldriver },
#endif

/* MISC */

#if NCTX > 0
	{ 0, &ctxdriver },
#endif
#if NSPIGOT > 0
	{ 0, &spigotdriver },
#endif

};

#define old_drivers_count (sizeof(old_drivers) / sizeof(old_drivers[0]))

