/*
 * H2CHAINS.C
 *
 * cc -I/usr/src/sys h2chains.c -o /usr/local/bin/h2chains -lkvm
 *
 * h2chains <hmpaddr>
 *
 * Copyright (c) 2017 The DragonFly Project.  All rights reserved.
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

static void h2chainscan(kvm_t *kd, int tab, uintptr_t cp,
			uintptr_t pcp, int pflags);
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
    hammer2_dev_t hmp;
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

    kkread(kd, base, &hmp, sizeof(hmp));
    cp = (uintptr_t)hmp.vchain.core.rbtree.rbh_root;
    printf("VCHAIN %08x\n", hmp.vchain.flags);
    if (cp)
	    h2chainscan(kd, 4, cp,
			base + offsetof(struct hammer2_dev, vchain),
			hmp.vchain.flags);
    printf("\n");

    cp = (uintptr_t)hmp.fchain.core.rbtree.rbh_root;
    printf("FCHAIN %08x\n", hmp.fchain.flags);
    if (cp)
	    h2chainscan(kd, 4, cp,
			base + offsetof(struct hammer2_dev, fchain),
			hmp.fchain.flags);

    return 0;
}

static
void
h2chainscan(kvm_t *kd, int tab, uintptr_t cp, uintptr_t pcp, int pflags)
{
	hammer2_chain_t chain;

	kkread(kd, cp, &chain, sizeof(chain));
	if (chain.rbnode.rbe_left)
		h2chainscan(kd, tab, (uintptr_t)chain.rbnode.rbe_left,
			    pcp, pflags);

	printf("%*.*s chain %p %08x type %02x ", tab, tab, "",
		(void *)cp, chain.flags, chain.bref.type);
	if (chain.flags & HAMMER2_CHAIN_ONFLUSH)
		printf("F");
	if (chain.flags & HAMMER2_CHAIN_MODIFIED)
		printf("M");
	if (chain.flags & (HAMMER2_CHAIN_ONFLUSH|HAMMER2_CHAIN_MODIFIED)) {
		if ((pflags & HAMMER2_CHAIN_ONFLUSH) == 0)
			printf(" FAIL");
	}
	if ((uintptr_t)chain.parent != pcp)
		printf(" FAIL2");
	printf("\n");
	if (chain.core.rbtree.rbh_root)
		h2chainscan(kd, tab + 4,
			    (uintptr_t)chain.core.rbtree.rbh_root,
			    cp, chain.flags);

	if (chain.rbnode.rbe_right)
		h2chainscan(kd, tab, (uintptr_t)chain.rbnode.rbe_right,
			    pcp, pflags);

}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
	if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
		perror("kvm_read");
		exit(1);
	}
}
