/*
 * TTYINFO.C
 *
 * cc -I/usr/src/sys ttyinfo.c -o /usr/local/bin/ttyinfo -lkvm
 *
 * ttyinfo
 *
 *
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/test/debug/ttyinfo.c,v 1.2 2004/10/08 18:32:58 dillon Exp $
 */

#define _KERNEL_STRUCTURES_
#include <sys/param.h>
#include <sys/user.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/namecache.h>
#include <sys/tty.h>
#include <sys/clist.h>

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

struct nlist Nl[] = {
    { "_cfreelist" },
    { "_cfreecount" },
    { "_cslushcount" },
    { "_ctotcount" },
    { NULL }
};

static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);
static int scanfree(kvm_t *kd, struct cblock *cfree);

int
main(int ac, char **av)
{
    struct cblock *cfree;
    int cbytes;
    int count;
    int slush;
    int totalcnt;
    int ch;
    kvm_t *kd;
    const char *corefile = NULL;
    const char *sysfile = NULL;

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

    if ((kd = kvm_open(sysfile, corefile, NULL, O_RDONLY, "kvm:")) == NULL) {
	perror("kvm_open");
	exit(1);
    }
    if (kvm_nlist(kd, Nl) != 0) {
	perror("kvm_nlist");
	exit(1);
    }
    kkread(kd, Nl[0].n_value, &cfree, sizeof(cfree));
    kkread(kd, Nl[1].n_value, &cbytes, sizeof(cbytes));
    kkread(kd, Nl[2].n_value, &slush, sizeof(slush));
    kkread(kd, Nl[3].n_value, &totalcnt, sizeof(totalcnt));
    count = scanfree(kd, cfree);
    printf("blksize %zd, freespc %d bytes, %zd blks (%d total), %d slush",
	CBSIZE, cbytes, cbytes / CBSIZE, totalcnt, slush);
    if (cbytes % CBSIZE)
	printf(" [unaligned]\n");
    else
	printf(" [aligned]\n");
    printf("freelist found to have %d blocks\n", count);
    return(0);
}

static int
scanfree(kvm_t *kd, struct cblock *cfree)
{
    int count = 0;
    struct cblock cb;

    while (cfree) {
	kkread(kd, (u_long)cfree, &cb, sizeof(cb));
	cfree = cb.c_head.ch_next;
	++count;
    }
    return(count);
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}

