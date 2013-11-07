/*
 * VMPAGEINFO.C
 *
 * cc -I/usr/src/sys vmpageinfo.c -o /usr/local/bin/vmpageinfo -lkvm
 *
 * vmpageinfo
 *
 * Validate the vm_page_buckets[] hash array against the vm_page_array
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
 * $DragonFly: src/test/debug/vmpageinfo.c,v 1.2 2006/05/23 01:00:05 dillon Exp $
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
#if 0
    { "_vm_page_buckets" },
    { "_vm_page_hash_mask" },
#endif
    { "_vm_page_array" },
    { "_vm_page_array_size" },
    { NULL }
};

int debugopt;
int verboseopt;
#if 0
struct vm_page **vm_page_buckets;
int vm_page_hash_mask;
#endif
struct vm_page *vm_page_array;
int vm_page_array_size;

void checkpage(kvm_t *kd, vm_page_t mptr, vm_page_t m, struct vm_object *obj);
static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);
static int kkread_err(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

static void addsltrack(vm_page_t m);
static void dumpsltrack(kvm_t *kd);

int
main(int ac, char **av)
{
    const char *corefile = NULL;
    const char *sysfile = NULL;
    vm_page_t mptr;
    struct vm_page m;
    struct vm_object obj;
    kvm_t *kd;
    int ch;
    int hv;
    int i;
    const char *qstr;
    const char *ostr;

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

#if 0
    kkread(kd, Nl[0].n_value, &vm_page_buckets, sizeof(vm_page_buckets));
    kkread(kd, Nl[1].n_value, &vm_page_hash_mask, sizeof(vm_page_hash_mask));
#endif
    kkread(kd, Nl[0].n_value, &vm_page_array, sizeof(vm_page_array));
    kkread(kd, Nl[1].n_value, &vm_page_array_size, sizeof(vm_page_array_size));

    /*
     * Scan the vm_page_array validating all pages with associated objects
     */
    for (i = 0; i < vm_page_array_size; ++i) {
	if (debugopt) {
	    printf("page %d\r", i);
	    fflush(stdout);
	}
	kkread(kd, (u_long)&vm_page_array[i], &m, sizeof(m));
	if (m.object) {
	    kkread(kd, (u_long)m.object, &obj, sizeof(obj));
	    checkpage(kd, &vm_page_array[i], &m, &obj);
	}
	if (verboseopt) {
	    if (m.queue >= PQ_HOLD) {
		qstr = "HOLD";
	    } else if (m.queue >= PQ_CACHE) {
		qstr = "CACHE";
	    } else if (m.queue >= PQ_ACTIVE) {
		qstr = "ACTIVE";
	    } else if (m.queue >= PQ_INACTIVE) {
		qstr = "INACTIVE";
	    } else if (m.queue >= PQ_FREE) {
		qstr = "FREE";
	    } else {
		qstr = "NONE";
	    } 
	    printf("page %p obj %p/%-8ju(%016jx) val=%02x dty=%02x hold=%d "
		   "wire=%-2d act=%-3d busy=%d %8s",
		&vm_page_array[i],
		m.object,
		(intmax_t)m.pindex,
		(intmax_t)m.pindex * PAGE_SIZE,
		m.valid,
		m.dirty,
		m.hold_count,
		m.wire_count,
		m.act_count,
		m.busy,
		qstr
	    );
	    if (m.object) {
		switch(obj.type) {
		case OBJT_DEFAULT:
		    ostr = "default";
		    break;
		case OBJT_SWAP:
		    ostr = "swap";
		    break;
		case OBJT_VNODE:
		    ostr = "vnode";
		    break;
		case OBJT_DEVICE:
		    ostr = "device";
		    break;
		case OBJT_PHYS:
		    ostr = "phys";
		    break;
		case OBJT_DEAD:
		    ostr = "dead";
		    break;
		default:
		    ostr = "unknown";
		    break;
		}
	    } else {
		ostr = "-";
	    }
	    printf(" %-7s", ostr);
	    if (m.flags & PG_BUSY)
		printf(" BUSY");
	    if (m.flags & PG_WANTED)
		printf(" WANTED");
	    if (m.flags & PG_WINATCFLS)
		printf(" WINATCFLS");
	    if (m.flags & PG_FICTITIOUS)
		printf(" FICTITIOUS");
	    if (m.flags & PG_WRITEABLE)
		printf(" WRITEABLE");
	    if (m.flags & PG_MAPPED)
		printf(" MAPPED");
	    if (m.flags & PG_ZERO)
		printf(" ZERO");
	    if (m.flags & PG_REFERENCED)
		printf(" REFERENCED");
	    if (m.flags & PG_CLEANCHK)
		printf(" CLEANCHK");
	    if (m.flags & PG_SWAPINPROG)
		printf(" SWAPINPROG");
	    if (m.flags & PG_NOSYNC)
		printf(" NOSYNC");
	    if (m.flags & PG_UNMANAGED)
		printf(" UNMANAGED");
	    if (m.flags & PG_MARKER)
		printf(" MARKER");
	    if (m.flags & PG_RAM)
		printf(" RAM");
	    if (m.flags & PG_SWAPPED)
		printf(" SWAPPED");
#if 0
	    if (m.flags & PG_SLAB)
		printf(" SLAB");
#endif
	    printf("\n");
#if 0
	    if (m.flags & PG_SLAB)
		addsltrack(&m);
#endif
	}
    }
    if (debugopt || verboseopt)
	printf("\n");

#if 0
    /*
     * Scan the vm_page_buckets array validating all pages found
     */
    for (i = 0; i <= vm_page_hash_mask; ++i) {
	if (debugopt) {
	    printf("index %d\r", i);
	    fflush(stdout);
	}
	kkread(kd, (u_long)&vm_page_buckets[i], &mptr, sizeof(mptr));
	while (mptr) {
	    kkread(kd, (u_long)mptr, &m, sizeof(m));
	    if (m.object) {
		kkread(kd, (u_long)m.object, &obj, sizeof(obj));
		hv = ((uintptr_t)m.object + m.pindex) ^ obj.hash_rand;
		hv &= vm_page_hash_mask;
		if (i != hv)
		    printf("vm_page_buckets[%d] ((struct vm_page *)%p)"
			" should be in bucket %d\n", i, mptr, hv);
		checkpage(kd, mptr, &m, &obj);
	    } else {
		printf("vm_page_buckets[%d] ((struct vm_page *)%p)"
			" has no object\n", i, mptr);
	    }
	    mptr = m.hnext;
	}
    }
#endif
    if (debugopt)
	printf("\n");
    dumpsltrack(kd);
    return(0);
}

/*
 * A page with an object.
 */
void
checkpage(kvm_t *kd, vm_page_t mptr, vm_page_t m, struct vm_object *obj)
{
#if 0
    struct vm_page scan;
    vm_page_t scanptr;
    int hv;

    hv = ((uintptr_t)m->object + m->pindex) ^ obj->hash_rand;
    hv &= vm_page_hash_mask;
    kkread(kd, (u_long)&vm_page_buckets[hv], &scanptr, sizeof(scanptr));
    while (scanptr) {
	if (scanptr == mptr)
	    break;
	kkread(kd, (u_long)scanptr, &scan, sizeof(scan));
	scanptr = scan.hnext;
    }
    if (scanptr) {
	if (debugopt > 1)
	    printf("good checkpage %p bucket %d\n", mptr, hv);
    } else {
	printf("vm_page_buckets[%d] ((struct vm_page *)%p)"
		" page not found in bucket list\n", hv, mptr);
    }
#endif
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

struct SLTrack {
        struct SLTrack *next;
        u_long addr;
};

#define SLHSIZE 1024
#define SLHMASK (SLHSIZE - 1)

struct SLTrack *SLHash[SLHSIZE];

static
void
addsltrack(vm_page_t m)
{
	struct SLTrack *slt;
	u_long addr = (m->pindex * PAGE_SIZE) & ~131071L;
	int i;

	if (m->wire_count == 0 || (m->flags & PG_MAPPED) == 0 ||
	    m->object == NULL)
		return;

	i = (addr / 131072) & SLHMASK;
	for (slt = SLHash[i]; slt; slt = slt->next) {
		if (slt->addr == addr)
			break;
	}
	if (slt == NULL) {
		slt = malloc(sizeof(*slt));
		slt->addr = addr;
		slt->next = SLHash[i];
		SLHash[i] = slt;
	}
}

static
void
dumpsltrack(kvm_t *kd)
{
	struct SLTrack *slt;
	int i;
	long total_zones = 0;
	long full_zones = 0;

	for (i = 0; i < SLHSIZE; ++i) {
		for (slt = SLHash[i]; slt; slt = slt->next) {
			SLZone z;

			if (kkread_err(kd, slt->addr, &z, sizeof(z))) {
				printf("SLZone 0x%016lx not mapped\n",
					slt->addr);
				continue;
			}
			printf("SLZone 0x%016lx { mag=%08x cpu=%-2d NFree=%-3d "
			       "chunksz=%-5d }\n",
			       slt->addr,
			       z.z_Magic,
			       z.z_Cpu,
			       z.z_NFree,
			       z.z_ChunkSize
			);
			++total_zones;
			if (z.z_NFree == 0)
				++full_zones;
		}
	}
	printf("FullZones/TotalZones: %ld/%ld\n", full_zones, total_zones);
}
