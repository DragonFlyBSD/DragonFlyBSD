/*
 * VMPAGEINFO.C
 *
 * cc -I/usr/src/sys vmpageinfo.c -o ~/bin/vmpageinfo -lkvm
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
    { "_vm_page_array" },
    { "_vm_page_array_size" },
    { "_kernel_object" },
    { "_nbuf" },
    { "_nswbuf_mem" },
    { "_nswbuf_kva" },
    { "_nswbuf_raw" },
    { "_kernbase" },
    { "__end" },
    { NULL }
};

int debugopt;
int verboseopt;
#if 0
struct vm_page **vm_page_buckets;
int vm_page_hash_mask;
#endif
struct vm_page *vm_page_array;
struct vm_object *kernel_object_ptr;
int vm_page_array_size;
long nbuf;
long nswbuf_mem;
long nswbuf_kva;
long nswbuf_raw;
long kern_size;

void checkpage(kvm_t *kd, vm_page_t mptr, vm_page_t m, struct vm_object *obj);
static void kkread_vmpage(kvm_t *kd, u_long addr, vm_page_t m);
static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);
static int kkread_err(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

#if 0
static void addsltrack(vm_page_t m);
static void dumpsltrack(kvm_t *kd);
#endif
static int unique_object(void *ptr);

long count_free;
long count_wired;		/* total */
long count_wired_vnode;
long count_wired_anon;
long count_wired_in_pmap;
long count_wired_pgtable;
long count_wired_other;
long count_wired_kernel;
long count_wired_obj_other;

long count_anon;
long count_anon_in_pmap;
long count_vnode;
long count_device;
long count_phys;
long count_kernel;
long count_unknown;
long count_noobj_offqueue;
long count_noobj_onqueue;

int
main(int ac, char **av)
{
    const char *corefile = NULL;
    const char *sysfile = NULL;
    struct vm_page m;
    struct vm_object obj;
    kvm_t *kd;
    int ch;
#if 0
    vm_page_t mptr;
    int hv;
#endif
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

    kkread(kd, Nl[0].n_value, &vm_page_array, sizeof(vm_page_array));
    kkread(kd, Nl[1].n_value, &vm_page_array_size, sizeof(vm_page_array_size));
    kernel_object_ptr = (void *)Nl[2].n_value;
    kkread(kd, Nl[3].n_value, &nbuf, sizeof(nbuf));
    kkread(kd, Nl[4].n_value, &nswbuf_mem, sizeof(nswbuf_mem));
    kkread(kd, Nl[5].n_value, &nswbuf_kva, sizeof(nswbuf_kva));
    kkread(kd, Nl[6].n_value, &nswbuf_raw, sizeof(nswbuf_raw));
    kern_size = Nl[8].n_value - Nl[7].n_value;

    /*
     * Scan the vm_page_array validating all pages with associated objects
     */
    for (i = 0; i < vm_page_array_size; ++i) {
	if (debugopt) {
	    printf("page %d\r", i);
	    fflush(stdout);
	}
	kkread_vmpage(kd, (u_long)&vm_page_array[i], &m);
	if (m.object) {
	    kkread(kd, (u_long)m.object, &obj, sizeof(obj));
	    checkpage(kd, &vm_page_array[i], &m, &obj);
	}
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
	    ++count_free;
	} else {
	    qstr = "NONE";
	}
	if (m.wire_count) {
		++count_wired;
		if (m.object == NULL) {
			if ((m.flags & PG_MAPPED) &&
			    (m.flags & PG_WRITEABLE) &&
			    (m.flags & PG_UNQUEUED)) {
				++count_wired_pgtable;
			} else {
				++count_wired_other;
			}
		} else if (m.object == kernel_object_ptr) {
			++count_wired_kernel;
		} else {
			switch(obj.type) {
			case OBJT_VNODE:
				++count_wired_vnode;
				break;
			case OBJT_DEFAULT:
			case OBJT_SWAP:
				if (m.md.pmap_count)
					++count_wired_in_pmap;
				else
					++count_wired_anon;
				break;
			default:
				++count_wired_obj_other;
				break;
			}
		}
	} else if (m.md.pmap_count) {
		if (m.object && m.object != kernel_object_ptr) {
			switch(obj.type) {
			case OBJT_DEFAULT:
			case OBJT_SWAP:
				++count_anon_in_pmap;
				break;
			default:
				break;
			}
		}
	}

	if (verboseopt) {
	    printf("page %p obj %p/%-8ju(%016jx) val=%02x dty=%02x hold=%d "
		   "wire=%-2d act=%-3d busy=%d w/pmapcnt=%d/%d %8s",
		&vm_page_array[i],
		m.object,
		(intmax_t)m.pindex,
		(intmax_t)m.pindex * PAGE_SIZE,
		m.valid,
		m.dirty,
		m.hold_count,
		m.wire_count,
		m.act_count,
		m.busy_count,
		m.md.writeable_count,
		m.md.pmap_count,
		qstr
	    );
	}

	if (m.object == kernel_object_ptr) {
		ostr = "kernel";
		if (unique_object(m.object))
			count_kernel += obj.resident_page_count;
	} else if (m.object) {
	    switch(obj.type) {
	    case OBJT_DEFAULT:
		ostr = "default";
		if (unique_object(m.object))
			count_anon += obj.resident_page_count;
		break;
	    case OBJT_SWAP:
		ostr = "swap";
		if (unique_object(m.object))
			count_anon += obj.resident_page_count;
		break;
	    case OBJT_VNODE:
		ostr = "vnode";
		if (unique_object(m.object))
			count_vnode += obj.resident_page_count;
		break;
	    case OBJT_DEVICE:
		ostr = "device";
		if (unique_object(m.object))
			count_device += obj.resident_page_count;
		break;
	    case OBJT_PHYS:
		ostr = "phys";
		if (unique_object(m.object))
			count_phys += obj.resident_page_count;
		break;
	    case OBJT_DEAD:
		ostr = "dead";
		if (unique_object(m.object))
			count_unknown += obj.resident_page_count;
		break;
	    default:
		if (unique_object(m.object))
			count_unknown += obj.resident_page_count;
		ostr = "unknown";
		break;
	    }
	} else {
	    ostr = "-";
	    if (m.queue == PQ_NONE)
		    ++count_noobj_offqueue;
	    else if (m.queue - m.pc != PQ_FREE)
		    ++count_noobj_onqueue;
	}

	if (verboseopt) {
	    printf(" %-7s", ostr);
	    if (m.busy_count & PBUSY_LOCKED)
		printf(" BUSY");
	    if (m.busy_count & PBUSY_WANTED)
		printf(" WANTED");
	    if (m.flags & PG_WINATCFLS)
		printf(" WINATCFLS");
	    if (m.flags & PG_FICTITIOUS)
		printf(" FICTITIOUS");
	    if (m.flags & PG_WRITEABLE)
		printf(" WRITEABLE");
	    if (m.flags & PG_MAPPED)
		printf(" MAPPED");
	    if (m.flags & PG_NEED_COMMIT)
		printf(" NEED_COMMIT");
	    if (m.flags & PG_REFERENCED)
		printf(" REFERENCED");
	    if (m.flags & PG_CLEANCHK)
		printf(" CLEANCHK");
	    if (m.busy_count & PBUSY_SWAPINPROG)
		printf(" SWAPINPROG");
	    if (m.flags & PG_NOSYNC)
		printf(" NOSYNC");
	    if (m.flags & PG_UNQUEUED)
		printf(" UNQUEUED");
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
    printf("%8.2fM free\n", count_free * 4096.0 / 1048576.0);

    printf("%8.2fM wired vnode (in buffer cache)\n",
	count_wired_vnode * 4096.0 / 1048576.0);
    printf("%8.2fM wired in-pmap (probably vnode pages also in buffer cache)\n",
	count_wired_in_pmap * 4096.0 / 1048576.0);
    printf("%8.2fM wired pgtable\n",
	count_wired_pgtable * 4096.0 / 1048576.0);
    printf("%8.2fM wired anon\n",
	count_wired_anon * 4096.0 / 1048576.0);
    printf("%8.2fM wired kernel_object\n",
	count_wired_kernel * 4096.0 / 1048576.0);

	printf("\t%8.2fM vm_page_array\n",
	    vm_page_array_size * sizeof(struct vm_page) / 1048576.0);
	printf("\t%8.2fM buf, swbuf_mem, swbuf_kva, swbuf_raw\n",
	    (nbuf + nswbuf_mem + nswbuf_kva + nswbuf_raw) *
	    sizeof(struct buf) / 1048576.0);
	printf("\t%8.2fM kernel binary\n", kern_size / 1048576.0);
	printf("\t(also add in KMALLOC id kmapinfo, or loosely, vmstat -m)\n");

    printf("%8.2fM wired other (unknown object)\n",
	count_wired_obj_other * 4096.0 / 1048576.0);
    printf("%8.2fM wired other (no object, probably kernel)\n",
	count_wired_other * 4096.0 / 1048576.0);

    printf("%8.2fM WIRED TOTAL\n",
	count_wired * 4096.0 / 1048576.0);

    printf("\n");
    printf("%8.2fM anonymous (total, includes in-pmap)\n",
	count_anon * 4096.0 / 1048576.0);
    printf("%8.2fM anonymous memory in-pmap\n",
	count_anon_in_pmap * 4096.0 / 1048576.0);
    printf("%8.2fM vnode (includes wired)\n",
	count_vnode * 4096.0 / 1048576.0);
    printf("%8.2fM device\n", count_device * 4096.0 / 1048576.0);
    printf("%8.2fM phys\n", count_phys * 4096.0 / 1048576.0);
    printf("%8.2fM kernel (includes wired)\n",
	count_kernel * 4096.0 / 1048576.0);
    printf("%8.2fM unknown\n", count_unknown * 4096.0 / 1048576.0);
    printf("%8.2fM no_object, off queue (includes wired w/o object)\n",
	count_noobj_offqueue * 4096.0 / 1048576.0);
    printf("%8.2fM no_object, on non-free queue (includes wired w/o object)\n",
	count_noobj_onqueue * 4096.0 / 1048576.0);

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
#if 0
    dumpsltrack(kd);
#endif
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

/*
 * Acclerate the reading of VM pages
 */
static void
kkread_vmpage(kvm_t *kd, u_long addr, vm_page_t m)
{
    static struct vm_page vpcache[1024];
    static u_long vpbeg;
    static u_long vpend;

    if (addr < vpbeg || addr >= vpend) {
	vpbeg = addr;
	vpend = addr + 1024 * sizeof(*m);
	if (vpend > (u_long)(uintptr_t)vm_page_array +
		    vm_page_array_size * sizeof(*m)) {
	    vpend = (u_long)(uintptr_t)vm_page_array +
		    vm_page_array_size * sizeof(*m);
	}
	kkread(kd, vpbeg, vpcache, vpend - vpbeg);
    }
    *m = vpcache[(addr - vpbeg) / sizeof(*m)];
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

#if 0
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
#endif

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

#define HASH_SIZE	(1024*1024)
#define HASH_MASK	(HASH_SIZE - 1)

struct dup_entry {
	struct dup_entry *next;
	void	*ptr;
};

struct dup_entry *dup_hash[HASH_SIZE];

static int
unique_object(void *ptr)
{
	struct dup_entry *hen;
	int hv;

	hv = (intptr_t)ptr ^ ((intptr_t)ptr >> 20);
	hv &= HASH_MASK;
	for (hen = dup_hash[hv]; hen; hen = hen->next) {
		if (hen->ptr == ptr)
			return 0;
	}
	hen = malloc(sizeof(*hen));
	hen->next = dup_hash[hv];
	hen->ptr = ptr;
	dup_hash[hv] = hen;

	return 1;
}
