/*
 * KMAPINFO.C
 *
 * cc -I/usr/src/sys kmapinfo.c -o ~/bin/kmapinfo -lkvm
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
vm_offset_t total_used_byid[VM_SUBSYS_LIMIT];

static const char *formatnum(int64_t value);
static const char *entryid(vm_subsys_t id);
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
    int i;
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

    printf("%4ldM 0x%016jx %08lx-%08lx (%6s) EMPTY\n",
	total_used / 1024 / 1024,
	(intmax_t)NULL,
	last, kmap.header.end,
	formatnum(kmap.header.end - last));
    total_empty += kmap.header.end - last;

    printf("-----------------------------------------------\n");
    for (i = 0; i < VM_SUBSYS_LIMIT; ++i)
	printf("Total-id: %9s %s\n", entryid(i), formatnum(total_used_byid[i]));

    printf("-----------------------------------------------\n");
    printf("Total empty space: %s\n", formatnum(total_empty));
    printf("Total used  space: %s\n", formatnum(total_used));
}

static const char *
formatnum(int64_t value)
{
	static char buf[64];
	const char *neg;
	const char *suffix;
	int64_t div = 1;

	if (value < 0) {
		value = -value;
		neg = "-";
	} else {
		neg = "";
	}
	if (value < 100000) {
		div = 1;
		suffix = "";
	} else if (value < 1 * 1024 * 1024) {
		div = 1024;
		suffix = "K";
	} else if (value < 1024 * 1024 * 1024) {
		div = 1024 * 1024;
		suffix = "M";
	} else if (value < 1024LL * 1024 * 1024 * 1024) {
		div = 1024 * 1024 * 1024;
		suffix = "G";
	} else {
		div = 1024LL * 1024 * 1024 * 1024;
		suffix = "T";
	}
	if (value == 0) {
		snprintf(buf, sizeof(buf), "");
	} else if (div == 1) {
		snprintf(buf, sizeof(buf), "%s%7.0f%s",
			 neg,
			 (double)value / (double)div,
			 suffix);
	} else {
		snprintf(buf, sizeof(buf), "%s%6.2f%s",
			 neg,
			 (double)value / (double)div,
			 suffix);
	}
	return buf;
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
    printf("%4ldM %p %08lx-%08lx (%6ldK) id=%-8s object=%p\n",
	total_used / 1024 / 1024,
	entryp,
	entry.start, entry.end,
	(entry.end - entry.start) / 1024,
	entryid(entry.id),
	entry.object.map_object);
    total_used += entry.end - entry.start;
    if (entry.id < VM_SUBSYS_LIMIT)
	total_used_byid[entry.id] += entry.end - entry.start;
    else
	total_used_byid[0] += entry.end - entry.start;
    *lastp = entry.end;
    mapscan(kd, entry.rb_entry.rbe_right, lastp);
}

static
const char *
entryid(vm_subsys_t id)
{
	static char buf[32];

	switch(id) {
	case VM_SUBSYS_UNKNOWN:
		return("UNKNOWN");
	case VM_SUBSYS_KMALLOC:
		return("KMALLOC");
	case VM_SUBSYS_STACK:
		return("STACK");
	case VM_SUBSYS_IMGACT:
		return("IMGACT");
	case VM_SUBSYS_EFI:
		return("EFI");
	case VM_SUBSYS_RESERVED:
		return("RESERVED");
	case VM_SUBSYS_INIT:
		return("INIT");
	case VM_SUBSYS_PIPE:
		return("PIPE");
	case VM_SUBSYS_PROC:
		return("PROC");
	case VM_SUBSYS_SHMEM:
		return("SHMEM");
	case VM_SUBSYS_SYSMAP:
		return("SYSMAP");
	case VM_SUBSYS_MMAP:
		return("MMAP");
	case VM_SUBSYS_BRK:
		return("BRK");
	case VM_SUBSYS_BOGUS:
		return("BOGUS");
	case VM_SUBSYS_BUF:
		return("BUF");
	case VM_SUBSYS_BUFDATA:
		return("BUFDATA");
	case VM_SUBSYS_GD:
		return("GD");
	case VM_SUBSYS_IPIQ:
		return("IPIQ");
	case VM_SUBSYS_PVENTRY:
		return("PVENTRY");
	case VM_SUBSYS_PML4:
		return("PML4");
	case VM_SUBSYS_MAPDEV:
		return("MAPDEV");
	case VM_SUBSYS_ZALLOC:
		return("ZALLOC");

	case VM_SUBSYS_DM:
		return("DM");
	case VM_SUBSYS_CONTIG:
		return("CONTIG");
	case VM_SUBSYS_DRM:
		return("DRM");
	case VM_SUBSYS_DRM_GEM:
		return("DRM_GEM");
	case VM_SUBSYS_DRM_SCAT:
		return("DRM_SCAT");
	case VM_SUBSYS_DRM_VMAP:
		return("DRM_VMAP");
	case VM_SUBSYS_DRM_TTM:
		return("DRM_TTM");
	case VM_SUBSYS_HAMMER:
		return("HAMMER");
	default:
		break;
	}
	snprintf(buf, sizeof(buf), "%d", (int)id);

	return buf;
}


static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}
