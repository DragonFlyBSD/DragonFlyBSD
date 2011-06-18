/*
 * Copyright (c) 2011 Alex Hornung <alex@alexhornung.com>.
 * All rights reserved.
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/libkern.h>
#include <sys/random.h>

#include <dev/crypto/padlock/padlock.h>

static int random_count = 16;

static __inline void
padlock_rng(int *out, size_t count)
{
	unsigned int status;

	/*
	 * xstore-rng:
	 * eax: (output) RNG status word
	 * ecx: (input)  rep. count
	 * edx: (input)  quality factor (0-3)
	 * edi: (input)  buffer for random data
	 */
	__asm __volatile(
		"pushf			\n\t"
		"popf			\n\t"
		"rep			\n\t"
		".byte  0x0f, 0xa7, 0xc0"
			: "=a" (status)
			: "d" (2), "D" (out), "c" (count*sizeof(*out))
			: "cc", "memory"
	);
}

static void
padlock_rng_harvest(void *arg)
{
	struct padlock_softc *sc = arg;
	int randomness[128];
	int *arandomness; /* randomness aligned */
	int i;

	arandomness = PADLOCK_ALIGN(randomness);
	padlock_rng(arandomness, random_count);

	for (i = 0; i < random_count; i++)
		add_true_randomness(arandomness[i]);

	callout_reset(&sc->sc_rng_co, sc->sc_rng_ticks,
	    padlock_rng_harvest, sc);
}

void
padlock_rng_init(struct padlock_softc *sc)
{
	if (hz > 100)
		sc->sc_rng_ticks = hz/100;
	else
		sc->sc_rng_ticks = 1;

	callout_init_mp(&sc->sc_rng_co);
	callout_reset(&sc->sc_rng_co, sc->sc_rng_ticks,
	    padlock_rng_harvest, sc);
}

void
padlock_rng_uninit(struct padlock_softc *sc)
{
	callout_stop(&sc->sc_rng_co);
}

