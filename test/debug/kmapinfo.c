/*
 * KMAPINFO.C
 *
 * cc -I/usr/src/sys kmapinfo.c -o /usr/local/bin/kmapinfo -lkvm
 *
 * Dump the kernel_map
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
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/namecache.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_map.h>
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
    { "_kernel_map" },
    { NULL }
};

int debugopt;
int verboseopt;
struct vm_map kmap;
vm_offset_t total_empty;
vm_offset_t total_used;

static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);
static void mapscan(kvm_t *kd, vm_map_entry_t entry,
		    vm_offset_t *lastp);

int
main(int ac, char **av)
{
    const char *corefile = NULL;
    const char *sysfile = NULL;
    kvm_t *kd;
    int ch;
    vm_offset_t last;

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
    kkread(kd, Nl[0].n_value, &kmap, sizeof(kmap));
    last = kmap.header.start;
    mapscan(kd, kmap.rb_root.rbh_root, &last);

    printf("%4ldM 0x%016jx %08lx-%08lx (%6ldK) EMPTY\n",
	total_used / 1024 / 1024,
	(intmax_t)NULL,
	last, kmap.header.end,
	(kmap.header.end - last) / 1024);
    total_empty += kmap.header.end - last;

    printf("-----------------------------------------------\n");
    printf("Total empty space: %7ldK\n", total_empty / 1024);
    printf("Total used  space: %7ldK\n", total_used / 1024);
}

static void
mapscan(kvm_t *kd, vm_map_entry_t entryp, vm_offset_t *lastp)
{
    struct vm_map_entry entry;

    if (entryp == NULL)
	return;
    kkread(kd, (u_long)entryp, &entry, sizeof(entry));
    mapscan(kd, entry.rb_entry.rbe_left, lastp);
    if (*lastp != entry.start) {
	    printf("%4ldM %p %08lx-%08lx (%6ldK) EMPTY\n",
		total_used / 1024 / 1024,
		entryp,
		*lastp, entry.start,
		(entry.start - *lastp) / 1024);
	    total_empty += entry.start - *lastp;
    }
    printf("%4ldM %p %08lx-%08lx (%6ldK) type=%d object=%p\n",
	total_used / 1024 / 1024,
	entryp,
	entry.start, entry.end,
	(entry.end - entry.start) / 1024,
	entry.maptype,
	entry.object.map_object);
    total_used += entry.end - entry.start;
    *lastp = entry.end;
    mapscan(kd, entry.rb_entry.rbe_right, lastp);
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}
