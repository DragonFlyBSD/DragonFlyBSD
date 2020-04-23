/*
 * SCHEDMON.C
 *
 * cc -I/usr/src/sys schedmon.c -o ~/bin/schedmon -lkvm
 *
 * Monitor the user scheduler
 *
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
 *
 * $DragonFly: src/test/debug/ncptrace.c,v 1.7 2007/05/06 20:45:01 dillon Exp $
 */

#define _KERNEL_STRUCTURES
#include <sys/param.h>
#include <sys/user.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/namecache.h>
#include <sys/mount.h>
#include <sys/cpu_topology.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>
#include <vm/swap_pager.h>
#include <vm/vnode_pager.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <getopt.h>

#include <sys/usched_dfly.h>

struct nlist Nl[] = {
    { "_ncpus" },
    { "_dfly_pcpu" },
    { NULL }
};

static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

int
main(int ac, char **av)
{
    struct usched_dfly_pcpu *pcpu;
    struct usched_dfly_pcpu *scan;
    const char *corefile = NULL;
    const char *sysfile = NULL;
    const char *path;
    kvm_t *kd;
    int ncpus;
    int ch;
    int i;

    while ((ch = getopt(ac, av, "M:N:")) != -1) {
	switch(ch) {
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
    usleep(100000);
    kkread(kd, Nl[0].n_value, &ncpus, sizeof(ncpus));
    pcpu = malloc(ncpus * sizeof(*pcpu));
    kkread(kd, Nl[1].n_value, pcpu, ncpus * sizeof(*pcpu));

    printf("%p %d\n", pcpu, ncpus);
    for (i = 0; i < ncpus; ++i) {
	scan = &pcpu[i];

	printf("%3d ", i);
	if (scan->ucount == 0 && scan->uload == 0 && scan->uschedcp == NULL) {
		printf("IDLE                                               "
			"                    ");
	} else {
		printf("runq=%-3d ucount=%-3d uload=%-5d ",
			scan->runqcount, scan->ucount, scan->uload);
		if (scan->uschedcp)
			printf("uschedcp=%p ", scan->uschedcp);
		else
			printf("uschedcp=------------------ ");
		if (scan->queuebits)
			printf("qs=%08x", scan->queuebits);
		else
			printf("qs=EMPTY   ");
	}
	if (i & 1)
		printf("\n");
	else
		printf("\t");
    }
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}
