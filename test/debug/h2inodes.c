/*
 * H2INODES.C
 *
 * cc -I/usr/src/sys h2inodes.c -o ~/bin/h2inodes -lkvm
 *
 * h2inodes <pfsaddr>
 *
 * Copyright (c) 2018 The DragonFly Project.  All rights reserved.
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

#include <sys/types.h>
#include <sys/user.h>
#include <vfs/hammer2/hammer2.h>

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

static void h2inumscan(kvm_t *kd, int tab, uintptr_t cp);
static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

#if 0
struct nlist Nl[] = {
    { "_rootnch" },
    { "_mountlist" },
    { NULL }
};
#endif

int
main(int ac, char **av)
{
    kvm_t *kd;
    const char *corefile = NULL;
    const char *sysfile = NULL;
    hammer2_pfs_t pmp;
    uintptr_t base;
    uintptr_t cp;
    int ch;

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
#if 0
    if (kvm_nlist(kd, Nl) != 0) {
	perror("kvm_nlist");
	exit(1);
    }
    kkread(kd, Nl[0].n_value, &nch, sizeof(nch));
    kkread(kd, Nl[1].n_value, &list, sizeof(list));
#endif

    base = strtoul(av[0], NULL, 0);

    kkread(kd, base, &pmp, sizeof(pmp));
    cp = (uintptr_t)pmp.inum_tree.rbh_root;
    if (cp)
	    h2inumscan(kd, 4, cp);
    printf("\n");
    return 0;
}

static
void
h2inumscan(kvm_t *kd, int tab, uintptr_t cp)
{
	hammer2_inode_t ip;

	kkread(kd, cp, &ip, sizeof(ip));
	if (ip.rbnode.rbe_left)
		h2inumscan(kd, tab, (uintptr_t)ip.rbnode.rbe_left);
	printf("%*.*s ip %p inum=%ld refs=%08x "
		"flags=%08x chain0=%p vp=%p\n",
		tab, tab, "", (void *)cp, ip.meta.inum, ip.refs,
		ip.flags, ip.cluster.array[0].chain, ip.vp);
	if (ip.rbnode.rbe_right)
		h2inumscan(kd, tab, (uintptr_t)ip.rbnode.rbe_right);
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
	if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
		perror("kvm_read");
		exit(1);
	}
}
