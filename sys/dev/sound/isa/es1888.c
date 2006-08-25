/*
 * Copyright (c) 1999 Doug Rabson
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
 * $FreeBSD: src/sys/dev/sound/isa/es1888.c,v 1.5.2.5 2002/04/22 15:49:30 cg Exp $
 * $DragonFly: src/sys/dev/sound/isa/Attic/es1888.c,v 1.5 2006/08/25 22:37:08 swildner Exp $
 */

#include <dev/sound/pcm/sound.h>
#include <dev/sound/isa/sb.h>

SND_DECLARE_FILE("$DragonFly: src/sys/dev/sound/isa/Attic/es1888.c,v 1.5 2006/08/25 22:37:08 swildner Exp $");

static int
es1888_identify(driver_t *driver, device_t parent)
{
	/*
	 * We do not suppot rescans
	 */
	if (device_get_state(parent) == DS_ATTACHED)
		return (0);

	return (ENXIO);
}

static device_method_t es1888_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	es1888_identify),

	{ 0, 0 }
};

static driver_t es1888_driver = {
	"pcm",
	es1888_methods,
	1,			/* no softc */
};

DRIVER_MODULE(snd_es1888, isa, es1888_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_es1888, snd_pcm, PCM_MINVER, PCM_PREFVER, PCM_MAXVER);
MODULE_VERSION(snd_es1888, 1);


