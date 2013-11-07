/*
 * VMOBJINFO.C
 *
 * cc -I/usr/src/sys vmobjinfo.c -o /usr/local/bin/vmobjinfo -lkvm
 *
 * Dump all vm_object's in the system
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

#define _KERNEL_STRUCTURES
#include <sys/param.h>
#include <sys/user.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>
#include <sys/namecache.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/buf.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/swap_pager.h>
#include <vm/vnode_pager.h>

#include <vfs/ufs/quota.h>
#include <vfs/ufs/inode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <getopt.h>

TAILQ_HEAD(object_q, vm_object);

struct nlist Nl[] = {
    { "_vm_object_list" },
    { NULL }
};

static void scan_vmobjs(kvm_t *kd, struct object_q *obj_list);
static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);

int
main(int ac, char **av)
{
    struct object_q obj_list;
    kvm_t *kd;
    int i;
    int ch;
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
    kkread(kd, Nl[0].n_value, &obj_list, sizeof(obj_list));
    scan_vmobjs(kd, &obj_list);
    return(0);
}

static void
scan_vmobjs(kvm_t *kd, struct object_q *obj_list)
{
    struct vm_object *op;
    struct vm_object obj;

    op = TAILQ_FIRST(obj_list);
    while (op) {
	kkread(kd, (long)op, &obj, sizeof(obj));

	printf("%p type=%d size=%016jx handle=%p swblocks=%d\n",
		op, obj.type, (intmax_t)obj.size, obj.handle,
		obj.swblock_count);

	op = TAILQ_NEXT(&obj, object_list);
    }
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}
