/*
 * PSINFO.C
 *
 * cc -I/usr/src/sys psinfo.c -o ~/bin/psinfo -lkvm
 *
 * Dump information about processes and threads in the system.
 *
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
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
#include <sys/sysctl.h>
#include <sys/thread.h>
#include <sys/proc.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>
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

#if 0
static const char *formatnum(int64_t value);
static const char *entryid(vm_subsys_t id);
static void mapscan(kvm_t *kd, vm_map_entry_t kptr, vm_map_entry_t ken,
		    vm_offset_t *lastp);
#endif
static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

static void dumpproc(kvm_t *kd, struct kinfo_proc *kp);

int
main(int ac, char **av)
{
    const char *corefile = NULL;
    const char *sysfile = NULL;
    kvm_t *kd;
    int ch;
    int i;
    int count;
    vm_offset_t last;
    struct vm_map_entry entry;
    struct vm_map_entry *kptr;
    struct kinfo_proc *kp;

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

    kp = kvm_getprocs(kd, KERN_PROC_ALL, 0, &count);
    for (i = 0; i < count; ++i) {
	dumpproc(kd, &kp[i]);
    }
    return 0;
}

/*
 * kp_paddr
 *
 * kp_flags
 * kp_stat
 * kp_lock
 * kp_acflag
 * kp_traceflag
 *
 * kp_fd
 *
 * kp_comm
 *
 * kp_uid
 * kp_oud
 * kp_vm_rssize
 * kp_vm_swrss
 *
 * kp_lwp
 *
 */
static void
dumpproc(kvm_t *kd, struct kinfo_proc *kp)
{
	struct proc p;
	struct vmspace vms;
	struct vm_map_entry entry;
	struct vm_map_backing *ba;
	struct vm_object obj;
	vm_map_entry_t kptr;
	int bacount;

	printf("Process %6d:%-3d %6.6s (%s)",
	       kp->kp_pid, kp->kp_lwp.kl_tid,
	       kp->kp_lwp.kl_wmesg, kp->kp_comm);

	if ((int)kp->kp_pid <= 0) {
		printf("\n");
		return;
	}
	kkread(kd, (u_long)kp->kp_paddr, &p, sizeof(p));
	kkread(kd, (u_long)p.p_vmspace, &vms, sizeof(vms));

	printf(" (%6.2fM resident, %6.2f wired)\n",
		vms.vm_pmap.pm_stats.resident_count * 4096.0 / 1048576.0,
		vms.vm_pmap.pm_stats.wired_count * 4096.0 / 1048576.0);

	kptr = kvm_vm_map_entry_first(kd, &vms.vm_map, &entry);
	while (kptr) {
		bacount = 0;
		printf("    entry %016jx-%016jx eflags=%04x prot=%02x max_prot=%02x",
		       entry.ba.start, entry.ba.end,
		       entry.eflags, entry.protection, entry.max_protection);
		ba = &entry.ba;
		bacount = 2;
		for (;;) {
			if (++bacount == 3) {
				printf("\n\t\t");
				bacount = 0;
			}
			printf(" [obj=%016jx,off=%016jx",
			      (uintptr_t)ba->object, ba->offset);
			if (ba->object) {
				kkread(kd, (u_long)ba->object, &obj, sizeof(obj));
				if (obj.flags & OBJ_DEAD)
					printf(",DEAD");
			}
			printf("]");

			if (ba->backing_ba == NULL)
				break;
			kkread(kd, (u_long)ba->backing_ba, ba, sizeof(*ba));
		}
		printf("\n");
		kptr = kvm_vm_map_entry_next(kd, kptr, &entry);
	}
}

#if 0


    kkread(kd, Nl[0].n_value, &kmap, sizeof(kmap));
    last = kmap.min_addr;

    kptr = kvm_vm_map_entry_first(kd, &kmap, &entry);
    while (kptr) {
	mapscan(kd, kptr, &entry, &last);
	kptr = kvm_vm_map_entry_next(kd, kptr, &entry);
    }

    printf("%4ldM 0x%016jx %08lx-%08lx (%6s) EMPTY\n",
	total_used / 1024 / 1024,
	(intmax_t)NULL,
	last, kmap.max_addr,
	formatnum(kmap.max_addr - last));
    total_empty += kmap.max_addr - last;

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
mapscan(kvm_t *kd, vm_map_entry_t kptr, vm_map_entry_t ken, vm_offset_t *lastp)
{
    if (*lastp != ken->ba.start) {
	    printf("%4ldM %p %08lx-%08lx (%s) EMPTY\n",
		total_used / 1024 / 1024,
		kptr,
		*lastp, ken->ba.start,
		formatnum(ken->ba.start - *lastp));
	    total_empty += ken->ba.start - *lastp;
    }
    printf("%4ldM %p %08lx-%08lx (%6ldK) id=%-8s object=%p\n",
	total_used / 1024 / 1024,
	kptr,
	ken->ba.start, ken->ba.end,
	(ken->ba.end - ken->ba.start) / 1024,
	entryid(ken->id),
	ken->object.map_object);
    total_used += ken->ba.end - ken->ba.start;
    if (ken->id < VM_SUBSYS_LIMIT)
	total_used_byid[ken->id] += ken->ba.end - ken->ba.start;
    else
	total_used_byid[0] += ken->ba.end - ken->ba.start;
    *lastp = ken->ba.end;
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
	case VM_SUBSYS_VMPGHASH:
		return("VMPGHASH");
	default:
		break;
	}
	snprintf(buf, sizeof(buf), "%d", (int)id);

	return buf;
}

#endif

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}
