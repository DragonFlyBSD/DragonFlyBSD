/*
 * SLABINFO.C
 *
 * cc -I/usr/src/sys slabinfo.c -o /usr/local/bin/slabinfo -lkvm
 *
 * slabinfo
 *
 * dump kernel slab allocator pcpu data and chains
 *
 * Copyright (c) 2012 The DragonFly Project.  All rights reserved.
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

#define _KERNEL_STRUCTURES
#include <sys/param.h>
#include <sys/user.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/namecache.h>
#include <sys/globaldata.h>
#include <machine/globaldata.h>
#include <sys/slaballoc.h>

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
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <getopt.h>

struct nlist Nl[] = {
    { "_CPU_prvspace" },
    { "_ncpus" },
    { NULL }
};

int debugopt;
int verboseopt;

int slzonedump(kvm_t *kd, SLZone *kslz);
void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

int
main(int ac, char **av)
{
    const char *corefile = NULL;
    const char *sysfile = NULL;
    kvm_t *kd;
    int ch;
    int i;
    int j;
    int ncpus;
    int totalzones;
    int totalfree;
    struct globaldata gd;

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

    kkread(kd, Nl[1].n_value, &ncpus, sizeof(int));
    totalzones = 0;
    totalfree = 0;
    for (i = 0; i < ncpus; ++i) {
	kkread(kd, Nl[0].n_value + i * sizeof(struct privatespace), &gd, sizeof(gd));
	printf("CPU %02d (NFreeZones=%d) {\n",
		i, gd.gd_slab.NFreeZones);
	totalfree += gd.gd_slab.NFreeZones;

	for (j = 0; j < NZONES; ++j) {
		printf("    Zone %02d {\n", j);
		totalzones += slzonedump(kd, gd.gd_slab.ZoneAry[j]);
		printf("    }\n");
	}

	printf("    FreeZone {\n");
	totalzones += slzonedump(kd, gd.gd_slab.FreeZones);
	printf("    }\n");

	printf("    FreeOVZon {\n");
	totalzones += slzonedump(kd, gd.gd_slab.FreeOvZones);
	printf("    }\n");

	printf("}\n");
    }
    printf("TotalZones %d x 131072 = %jd\n",
	totalzones, (intmax_t)totalzones * 131072LL);
    printf("TotalFree  %d x 131072 = %jd\n",
	totalfree, (intmax_t)totalfree * 131072LL);
    return(0);
}

int
slzonedump(kvm_t *kd, SLZone *kslz)
{
    SLZone slz;
    int count = 0;

    while (kslz) {
	kkread(kd, (u_long)kslz, &slz, sizeof(slz));
	printf("\t{ magic=%08x cpu=%d chunking=%d NFree=%d/%d RCnt=%d}\n",
		slz.z_Magic, slz.z_Cpu, slz.z_ChunkSize,
		slz.z_NFree, slz.z_NMax, slz.z_RCount);
	kslz = slz.z_Next;
	++count;
    }
    return(count);
}

void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}
