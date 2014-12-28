/*
 * VMOBJINFO.C
 *
 * cc -I/usr/src/sys vmobjinfo.c -o ~/bin/vmobjinfo -lkvm
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
    { "_vm_object_lists" },
    { "_nswdev" },
    { "_dmmax" },
    { NULL }
};

int VerboseOpt;
int nswdev;
int dmmax;
int memfds = -1;
int *swapfds;
char pgbuf[PAGE_SIZE];

static void scan_vmobjs(kvm_t *kd, struct object_q *obj_list);
static void dump_swap(kvm_t *kd, struct swblock *swbp);
static void dump_memq(kvm_t *kd, struct vm_page *pgp);
static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);
static off_t devoffset(long blkno, int *whichp);

int
main(int ac, char **av)
{
    struct object_q obj_list[VMOBJ_HSIZE];
    kvm_t *kd;
    int i;
    int nswap;
    int ch;
    const char *corefile = NULL;
    const char *sysfile = NULL;

    while ((ch = getopt(ac, av, "M:N:v")) != -1) {
	switch(ch) {
	case 'M':
	    corefile = optarg;
	    break;
	case 'N':
	    sysfile = optarg;
	    break;
	case 'v':
	    ++VerboseOpt;
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
    kkread(kd, Nl[1].n_value, &nswdev, sizeof(nswdev));
    kkread(kd, Nl[2].n_value, &dmmax, sizeof(dmmax));

    if (VerboseOpt) {
	swapfds = calloc(sizeof(int), nswdev);
	for (i = 0; i < nswdev && i < ac - optind; ++i) {
		printf("open %s\n", av[optind + i]);
		swapfds[i] = open(av[optind + i], O_RDONLY);
	}
	while (i < nswdev) {
		swapfds[i] = -1;
		++i;
	}
	memfds = open("/dev/mem", O_RDONLY);
    }

    kkread(kd, Nl[0].n_value, obj_list, sizeof(obj_list));
    for (i = 0; i < VMOBJ_HSIZE; ++i)
	    scan_vmobjs(kd, &obj_list[i]);
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
	printf("\t\t   ref_count=%d backing_obj=%p\n",
		obj.ref_count, obj.backing_object);

	if (VerboseOpt) {
		dump_swap(kd, obj.swblock_root.rbh_root);
		if (obj.type == OBJT_DEFAULT || obj.type == OBJT_SWAP)
			dump_memq(kd, obj.rb_memq.rbh_root);
	}

	op = TAILQ_NEXT(&obj, object_list);
    }
}

static void
dump_swap(kvm_t *kd, struct swblock *swbp)
{
	struct swblock swb;
	int which;
	int i;
	int j;
	int k;
	int fd;
	off_t off;

	if (swbp == NULL)
		return;
	kkread(kd, (long)swbp, &swb, sizeof(swb));
	dump_swap(kd, swb.swb_entry.rbe_left);

	for (i = 0; i < SWAP_META_PAGES; ++i) {
		printf("    %016lx: ", (swb.swb_index + i) * 4096L);
		if (swb.swb_pages[i] == SWAPBLK_NONE) {
			printf(" (unassigned)\n");
			continue;
		}
		printf(" %ld\n", swb.swb_pages[i]);
		off = devoffset(swb.swb_pages[i], &which);
		if (swapfds[which] >= 0) {
			lseek(swapfds[which], off, 0);
			if (read(swapfds[which], pgbuf, sizeof(pgbuf)) <= 0)
				printf("\t(read failed)\n");
			else
			for (j = 0; j < PAGE_SIZE; j += 16) {
				printf("\t%04x ", j);
				for (k = 0; k < 16; ++k) {
					printf(" %02x", (uint8_t)pgbuf[j+k]);
					if (k == 7)
						printf(" ");
				}
				printf("  ");
				for (k = 0; k < 16; ++k) {
					if (isprint((uint8_t)pgbuf[j+k]))
						printf("%c", pgbuf[j+k]);
					else
						printf(".");
				}
				printf("\n");
			}
		}
	}

	dump_swap(kd, swb.swb_entry.rbe_right);
}

static void
dump_memq(kvm_t *kd, struct vm_page *pgp)
{
	struct vm_page pg;
	int j;
	int k;

	if (pgp == NULL)
		return;
	kkread(kd, (long)pgp, &pg, sizeof(pg));
	dump_memq(kd, pg.rb_entry.rbe_left);
	printf("    %016lx: %016jx (physical)\n",
	       pg.pindex * 4096L, (intmax_t)pg.phys_addr);
	lseek(memfds, pg.phys_addr, 0);
	if (read(memfds, pgbuf, sizeof(pgbuf)) <= 0) {
		printf("\t(read failed)\n");
	} else {
		for (j = 0; j < PAGE_SIZE; j += 16) {
			printf("\t%04x ", j);
			for (k = 0; k < 16; ++k) {
				printf(" %02x", (uint8_t)pgbuf[j+k]);
				if (k == 7)
					printf(" ");
			}
			printf("  ");
			for (k = 0; k < 16; ++k) {
				if (isprint((uint8_t)pgbuf[j+k]))
					printf("%c", pgbuf[j+k]);
				else
					printf(".");
			}
			printf("\n");
		}
	}

	dump_memq(kd, pg.rb_entry.rbe_right);
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}

static off_t
devoffset(long blkno, int *whichp)
{
	off_t off;
	long seg;

	if (nswdev > 1) {
		off = blkno % dmmax;
		seg = blkno / dmmax;
		*whichp = seg % nswdev;
		seg /= nswdev;
		off = (off_t)(seg * dmmax + off) << PAGE_SHIFT;
	} else {
		*whichp = 0;
		off = blkno * PAGE_SIZE;
	}
}
