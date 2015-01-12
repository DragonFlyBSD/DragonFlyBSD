/*
 * ZALLOCINFO.C
 *
 * cc -I/usr/src/sys zallocinfo.c -o /usr/local/bin/zallocinfo -lkvm
 *
 * zallocinfo
 *
 * Print the slab structure and chains for all cpus.
 *
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
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

#define _KERNEL_STRUCTURES_
#include <sys/param.h>
#include <sys/user.h>
#include <sys/malloc.h>
#include <sys/slaballoc.h>
#include <sys/signalvar.h>
#include <sys/globaldata.h>
#include <machine/globaldata.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/swap_pager.h>
#include <vm/vnode_pager.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <err.h>
#include <getopt.h>

struct nlist Nl[] = {
    { "_CPU_prvspace" },
    { "_ncpus" },
    { NULL }
};

int debugopt;
int verboseopt;

static void dumpslab(kvm_t *kd, int cpu, struct SLGlobalData *slab);
static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

int
main(int ac, char **av)
{
    const char *corefile = NULL;
    const char *sysfile = NULL;
    struct SLGlobalData slab;
    struct mdglobaldata gd;
    u_long baseptr;
    kvm_t *kd;
    int offset;
    int ncpus;
    int ch;
    int cpu;

    while ((ch = getopt(ac, av, "M:N:dv")) != -1) {
	switch(ch) {
	case 'd':
	    ++debugopt;
	    break;
	case 'v':
	    ++verboseopt;
	    break;
	case 'M':
	    corefile = optarg;
	    break;
	case 'N':
	    sysfile = optarg;
	    break;
	default:
	    fprintf(stderr, "%s [-M core] [-N system]\n", av[0]);
	    exit(1);
	}
    }
    ac -= optind;
    av += optind;

    if ((kd = kvm_open(sysfile, corefile, NULL, O_RDONLY, "kvm:")) == NULL) {
	perror("kvm_open");
	exit(1);
    }
    if (kvm_nlist(kd, Nl) != 0) {
	perror("kvm_nlist");
	exit(1);
    }

    kkread(kd, Nl[1].n_value, &ncpus, sizeof(ncpus));

    for (cpu = 0; cpu < ncpus; cpu++) {
	    /*
	     * Get the CPU_prvspace base pointer, that is the address to
	     * CPU_prvspace[i] from where other addresses can be built
	     */
	    kkread(kd, Nl[0].n_value + cpu * sizeof(baseptr),
		    &baseptr, sizeof(baseptr));
	    /* Now get globaldata struct for the current cpu */
	    kkread(kd, baseptr + offsetof(struct mdglobaldata, mi.gd_slab),
		&slab, sizeof(slab));
	    dumpslab(kd, cpu, &slab);
    }
    printf("Done\n");
    return(0);
}

static void
dumpslab(kvm_t *kd, int cpu, struct SLGlobalData *slab)
{
    struct SLZone *zonep;
    struct SLZone zone;
    SLChunk *chunkp;
    SLChunk chunk;
    int z = 0;
    int rcount;
    int first;
    int64_t save;
    int64_t extra = 0;

    printf("cpu %d NFreeZones=%d JunkIndex=%d\n", cpu, slab->NFreeZones,
	slab->JunkIndex);

    zonep = LIST_FIRST(slab->ZoneAry);
    kkread(kd, (u_long)zonep, &zone, sizeof(zone));

    for (z = 0; z < NZONES; z++) {
	printf("    zone %2d", z);
	first = 1;
	save = extra;
	if (first)
		printf(" chunk=%-5d elms=%-4d free:",
		    zone.z_ChunkSize, zone.z_NMax);

	if (first == 0)
		printf(",");
	printf(" %d", zone.z_NFree);
	extra += zone.z_NFree * zone.z_ChunkSize;
	first = 0;

	chunkp = zone.z_RChunks;
	rcount = 0;
	while (chunkp) {
		kkread(kd, (u_long)chunkp, &chunk, sizeof(chunk));
		chunkp = chunk.c_Next;
		++rcount;
	}
	if (rcount) {
		printf(" rchunks=%d", rcount);
		extra += rcount * zone.z_ChunkSize;
	}
	chunkp = zone.z_LChunks;
	rcount = 0;
	while (chunkp) {
		kkread(kd, (u_long)chunkp, &chunk, sizeof(chunk));
		chunkp = chunk.c_Next;
		++rcount;
	}
	if (rcount) {
		printf(" lchunks=%d", rcount);
		extra += rcount * zone.z_ChunkSize;
	}
	printf(" (%jdK free)\n", (intmax_t)(extra - save) / 1024);
	zonep = LIST_NEXT(&zone, z_Entry);
	if (zonep)
		kkread(kd, (u_long)zonep, &zone, sizeof(zone));
    }
    printf("    TotalUnused %jdM\n", (intmax_t)extra / 1024 / 1024);
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
	    perror("kvm_read");
	    exit(1);
    }
}
