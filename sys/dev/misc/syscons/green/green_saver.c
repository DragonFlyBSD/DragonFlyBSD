/*-
 * (MPSAFE)
 *
 * Copyright (c) 1995-1998 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD: src/sys/modules/syscons/green/green_saver.c,v 1.17 1999/08/28 00:47:50 peter Exp $
 * $DragonFly: src/sys/dev/misc/syscons/green/green_saver.c,v 1.5 2007/09/06 18:17:24 swildner Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/consio.h>
#include <sys/fbio.h>
#include <sys/thread.h>

#include <dev/video/fb/fbreg.h>
#include <dev/video/fb/splashreg.h>
#include "../syscons.h"

static int blanked;

static int
green_saver(video_adapter_t *adp, int blank)
{

	if (blank == blanked) {
		return 0;
	}

	(*vidsw[adp->va_index]->blank_display)
		(adp, blank ? V_DISPLAY_STAND_BY : V_DISPLAY_ON);

	blanked = blank;

	return 0;
}

static int
green_init(video_adapter_t *adp)
{

	if ((*vidsw[adp->va_index]->blank_display)(adp, V_DISPLAY_ON) == 0) {
		return 0;
	}

	return ENODEV;
}

static int
green_term(video_adapter_t *adp)
{
	return 0;
}

static scrn_saver_t green_module = {
	"green_saver", green_init, green_term, green_saver, NULL,
};

SAVER_MODULE(green_saver, green_module);
