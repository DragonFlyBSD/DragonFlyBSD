/*
 * WILDCARDINFO.C
 *
 * cc -I/usr/src/sys wildcardinfo.c -o /usr/local/bin/wildcardinfo -lkvm
 *
 * wildcardinfo
 *
 * Dump the tcbinfo[] array and wildcard hash table for each cpu.
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
 * $DragonFly: src/test/debug/wildcardinfo.c,v 1.1 2005/04/05 02:49:15 dillon Exp $
 */

#define _KERNEL_STRUCTURES_
#include <sys/param.h>
#include <sys/user.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <net/route.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <getopt.h>

struct nlist Nl[] = {
    { "_ncpus" },
    { "_tcbinfo" },
    { NULL }
};

static void dumptcb(kvm_t *kd, intptr_t tcbinfo);
static void dumpinpcontainerhead(kvm_t *kd, int index, void *kptr);
static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

int
main(int ac, char **av)
{
    kvm_t *kd;
    int i;
    int ch;
    int ncpus;
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
    kkread(kd, Nl[0].n_value, &ncpus, sizeof(ncpus));
    for (i = 0; i < ncpus; ++i) {
	printf("CPU %d\n", i);
	dumptcb(kd, (intptr_t)Nl[1].n_value + i * sizeof(struct inpcbinfo));
    }
    return(0);
}

static
void
dumptcb(kvm_t *kd, intptr_t tcbaddr)
{
    struct inpcbinfo info;
    struct inpcbportinfo pinfo;
    intptr_t pinfoaddr;
    int i;

    kkread(kd, tcbaddr, &info, sizeof(info));
    kkread(kd, (intptr_t)info.portinfo, &pinfo, sizeof(pinfo));
    printf("    hashbase %p\n", info.hashbase);
    printf("    hashmask %ld\n", info.hashmask);
    printf("    porthashbase %p\n", pinfo.porthashbase);
    printf("    porthashmask %lu\n", pinfo.porthashmask);
    printf("    wildcardhashbase %p\n", info.wildcardhashbase);
    printf("    wildcardhashmask %lu\n", info.wildcardhashmask);
    printf("    lastport %d\n", (int)pinfo.lastport);
    printf("    lastlow %d\n", (int)pinfo.lastlow);
    printf("    lasthi %d\n", (int)pinfo.lasthi);
    printf("    ipi_size %zu\n", info.ipi_size);
    printf("    ipi_count %d\n", (int)info.ipi_count);
    printf("    ipi_gencnt %lld\n", (long long)info.ipi_gencnt);
    printf("    cpu %d\n", info.cpu);
    for (i = 0; i <= info.wildcardhashmask; ++i)
	dumpinpcontainerhead(kd, i, info.wildcardhashbase + i);
}

static
void
dumpinpcontainerhead(kvm_t *kd, int index, void *kptr)
{
    struct inpcontainerhead head;
    struct inpcontainer node;
    struct inpcb pcb;

    kkread(kd, (intptr_t)kptr, &head, sizeof(head));
    if (head.lh_first == NULL)
	return;
    printf("\tinpcontainer list at index %d {\n", index);
    for (kptr = head.lh_first; kptr; kptr = node.ic_list.le_next)  {
	kkread(kd, (intptr_t)kptr, &node, sizeof(node));
	printf("\t    inpcontainer %p inpcb %p", kptr, node.ic_inp);
	if (node.ic_inp) {
		printf(" {\n");
		kkread(kd, (intptr_t)node.ic_inp, &pcb, sizeof(pcb));
		printf("\t\tlocal %s:%d foreign %s:%d\n", 
			inet_ntoa(pcb.inp_inc.inc_laddr),
			ntohs(pcb.inp_inc.inc_lport),
			inet_ntoa(pcb.inp_inc.inc_faddr), 
			ntohs(pcb.inp_inc.inc_fport));
		printf("\t    }");
	}
	printf("\n");
    }
    printf("\t}\n");
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}

