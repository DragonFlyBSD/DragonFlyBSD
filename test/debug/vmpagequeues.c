/*
 * VMPAGEINFO.C
 *
 * cc -I/usr/src/sys vmpagequeues.c -o ~/bin/vmpagequeues -lkvm
 *
 * vmpagequeues
 *
 * Outputs statistics on PQ_FREE (-f, default), or -c, -i, -a for
 * PQ_CACHE, PQ_INACTIVE, and PQ_ACTIVE.
 *
 * Copyright (c) 2016 The DragonFly Project.  All rights reserved.
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
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/namecache.h>
#include <sys/slaballoc.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>
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
#if 0
    { "_vm_page_buckets" },
    { "_vm_page_hash_mask" },
#endif
    { "_vm_page_queues" },
    { NULL }
};

int debugopt;
int verboseopt;

static void kkread_vmpage(kvm_t *kd, u_long addr, vm_page_t m);
static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);
static int kkread_err(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

int
main(int ac, char **av)
{
    const char *corefile = NULL;
    const char *sysfile = NULL;
    kvm_t *kd;
    int ch;
    int i;
    int q = PQ_FREE;
    struct vpgqueues queues[PQ_COUNT];

    while ((ch = getopt(ac, av, "M:N:dviacf")) != -1) {
	switch(ch) {
	case 'i':
	    q = PQ_INACTIVE;
	    break;
	case 'a':
	    q = PQ_ACTIVE;
	    break;
	case 'c':
	    q = PQ_CACHE;
	    break;
	case 'f':
	    q = PQ_FREE;
	    break;
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

    for (;;) {
	kkread(kd, Nl[0].n_value, queues, sizeof(queues));
	for (i = 0; i < PQ_L2_SIZE; ++i) {
		struct vpgqueues *vpq = &queues[q+i];
		if ((i & 7) == 0)
			printf("%3d ", i);
		printf("\t%6d", vpq->lcnt);
		if ((i & 7) == 7)
			printf("\n");
		if ((i & 63) == 63)
			printf("\n");
	}
	printf("\n");
	sleep(1);
    }
    return(0);
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}

static int
kkread_err(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
	return 1;
    }
    return 0;
}
