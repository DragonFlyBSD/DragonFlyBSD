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
 * $DragonFly: src/test/debug/vmpageinfo.c,v 1.1 2004/11/09 21:50:12 dillon Exp $
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
    { "_vm_page_buckets" },
    { "_vm_page_hash_mask" },
    { "_vm_page_array" },
    { "_vm_page_array_size" },
    { NULL }
};

int debugopt;
struct vm_page **vm_page_buckets;
int vm_page_hash_mask;
struct vm_page *vm_page_array;
int vm_page_array_size;

void checkpage(kvm_t *kd, vm_page_t mptr, vm_page_t m, struct vm_object *obj);
void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

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

    while ((ch = getopt(ac, av, "M:N:d")) != -1) {
	switch(ch) {
	case 'd':
	    ++debugopt;
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

    kkread(kd, Nl[0].n_value, &vm_page_buckets, sizeof(vm_page_buckets));
    kkread(kd, Nl[1].n_value, &vm_page_hash_mask, sizeof(vm_page_hash_mask));
    kkread(kd, Nl[2].n_value, &vm_page_array, sizeof(vm_page_array));
    kkread(kd, Nl[3].n_value, &vm_page_array_size, sizeof(vm_page_array_size));

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
    }
    if (debugopt)
	printf("\n");

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
    if (debugopt)
	printf("\n");
    return(0);
}

/*
 * A page with an object.
 */
void
checkpage(kvm_t *kd, vm_page_t mptr, vm_page_t m, struct vm_object *obj)
{
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
}

void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}

