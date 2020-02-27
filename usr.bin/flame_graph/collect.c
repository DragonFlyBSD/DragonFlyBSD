/*
 * Copyright (c) 2020 The DragonFly Project.  All rights reserved.
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

#include "flame.h"

static void flame_disable(int sig);
static void flame_collect(int ncpus);
static void flame_doentry(int cpuid, uint32_t idx,
			struct flame_graph_entry *fge);

static uint32_t *savewindex;
static struct save_ctx symctx;

void
flame_collect_loop(void)
{
	int ncpus;
	int enable;
	int sniff = 1;
	int n;
	size_t ncpus_size = sizeof(ncpus);

	if (sysctlbyname("hw.ncpu", &ncpus, &ncpus_size, NULL, 0) < 0) {
		perror("hw.ncpu");
		exit(1);
	}
	savewindex = calloc(ncpus, sizeof(*savewindex));

	read_symbols("/boot/kernel/kernel");

	enable = 1;
	signal(SIGINT, flame_disable);
	if (sysctlbyname("debug.flame_graph_enable",
			 NULL, NULL, &enable, sizeof(enable)) < 0) {
		perror("debug.flame_graph_enable");
		exit(1);
	}

	n = 0;
	for (;;) {
		sysctlbyname("debug.flame_graph_sniff", NULL, NULL,
			     &sniff, sizeof(sniff));
		usleep(10000);
		++n;
		if ((n & 127) == 0)
			flame_collect(ncpus);
	}
	flame_disable(0);
}

static void
flame_collect(int ncpus)
{
	struct flame_graph_pcpu *fg;
	struct flame_graph_entry *fge;
	struct flame_graph_entry *scan;
	void *buf;
	void *ptr;
	size_t bytes;
	uint32_t rindex;
	uint32_t windex;
	uint32_t delta;
	int n;

	bytes = sizeof(size_t) +
		sizeof(struct flame_graph_pcpu) +
		sizeof(struct flame_graph_entry) * FLAME_GRAPH_NENTRIES;
	bytes *= ncpus;
	buf = malloc(bytes);

	if (sysctlbyname("debug.flame_graph_data", buf, &bytes, NULL, 0) < 0) {
		perror("debug.flame_graph_data");
		exit(1);
	}

	ptr = buf;
	for (n = 0; n < ncpus; ++n) {
		if (bytes < sizeof(size_t) + sizeof(*fg))
			break;
		fg = (void *)((char *)ptr + sizeof(size_t));
		bytes -= sizeof(size_t) + sizeof(*fg);
		ptr = (char *)ptr + sizeof(size_t) + sizeof(*fg);

		if (bytes < sizeof(*fge) * fg->nentries)
			break;
		fge = (void *)(fg + 1);
		ptr = (char *)ptr + sizeof(*fge) * fg->nentries;
		bytes -= sizeof(*fge) * fg->nentries;

		rindex = savewindex[n];
		windex = fg->windex;

		/*
		 * Lost data?
		 */
		delta = windex - rindex;
		if (delta >= fg->nentries)
			rindex = windex - fg->nentries + 1;

		/*
		 * Process entries
		 */
		while (rindex != windex) {
			scan = &fge[rindex % fg->nentries];
			if (scan->rips[0])
				flame_doentry(n, rindex, scan);
			++rindex;
		}
		savewindex[n] = windex;
	}
	free(buf);
}

static void
flame_doentry(int cpuid, uint32_t idx, struct flame_graph_entry *fge)
{
	int i;

	for (i = 0; i < FLAME_GRAPH_FRAMES; ++i) {
		if (fge->rips[i] == 0)
			break;
	}

	printf("%03d/%03d ", cpuid, idx % 1000);

	while (--i >= 0) {
		printf(" ");
		printf("%s()",
		       address_to_symbol((void *)fge->rips[i], &symctx));
	}
	printf("\n");
}

/*
 *
 */
static void
flame_disable(int sig __unused)
{
	int enable;

	enable = 0;
	if (sysctlbyname("debug.flame_graph_enable",
			 NULL, NULL, &enable, sizeof(enable)) < 0) {
		perror("debug.flame_graph_enable");
	}
	exit(0);
}
