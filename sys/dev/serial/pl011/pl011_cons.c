/*-
 * Copyright (c) 2026 The DragonFly Project.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/cons.h>
#include <sys/systm.h>

#include "pl011_reg.h"

#define	PL011_QEMU_BASE	0x09000000

static void
pl011_cnprobe(struct consdev *cp)
{
	cp->cn_pri = CN_REMOTE;
	cp->cn_probegood = 1;
}

static void
pl011_cninit(struct consdev *cp)
{
	cp->cn_private = (void *)PL011_QEMU_BASE;
}

static void
pl011_cnputc(void *arg, int c)
{
	volatile uint32_t *base = (volatile uint32_t *)arg;

	while (base[PL011_FR / 4] & PL011_FR_TXFF)
		;
	base[PL011_DR / 4] = (uint32_t)c;
}

CONS_DRIVER(pl011, pl011_cnprobe, pl011_cninit, NULL, NULL,
    NULL, NULL, pl011_cnputc, NULL, NULL);
