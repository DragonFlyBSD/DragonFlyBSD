/*
 * MBUFINFO.C
 *
 * cc -I/usr/src/sys mbufinfo.c -o /usr/local/bin/mbufinfo -lkvm
 *
 * mbufinfo
 *
 * Dump the MBUF_DEBUG mtrack tree.
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
#define MBUF_DEBUG
#include <sys/param.h>
#include <sys/user.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>
#include <sys/namecache.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/mbuf.h>
#include <sys/tree.h>

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

struct mbtrack {
        RB_ENTRY(mbtrack) rb_node;
	int trackid;
	struct mbuf *m;
};

RB_HEAD(mbuf_rb_tree, mbtrack);
RB_PROTOTYPE2(mbuf_rb_tree, mbtrack, rb_node, mbtrack_cmp, struct mbuf *);

struct nlist Nl[] = {
    { "_mbuf_track_root" },
    { NULL }
};

static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);
static void dumpmb(kvm_t *kd, struct mbtrack *mtp);
static void dumpmbdata(kvm_t *kd, char *data, int len);

struct mbuf_rb_tree tree;

int tracks[256];
int count_cluster;
int count_ext;
int count_noncluster;
int count_data_bytes;
int count_buffer_space;

int VerboseOpt;

int
main(int ac, char **av)
{
    kvm_t *kd;
    int i;
    int ch;
    const char *corefile = NULL;
    const char *sysfile = NULL;

    while ((ch = getopt(ac, av, "vM:N:")) != -1) {
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
    kkread(kd, Nl[0].n_value, &tree, sizeof(tree));
    if (tree.rbh_root)
	dumpmb(kd, tree.rbh_root);

    printf("Histogram:\n");
    for (i = 0; i < 256; ++i) {
	if (tracks[i]) {
		printf("%d\t%d\n", i, tracks[i]);
	}
    }
    printf("clusters: %d\n", count_cluster);
    printf("normal:   %d\n", count_noncluster);
    printf("external: %d\n", count_ext);
    printf("data:     %d\n", count_data_bytes);
    printf("bufspace: %d\n", count_buffer_space);
    return(0);
}

static void
dumpmb(kvm_t *kd, struct mbtrack *mtp)
{
    struct mbtrack mt;
    struct mbuf mb;

    kkread(kd, (long)mtp, &mt, sizeof(mt));

    if (mt.rb_node.rbe_left)
	dumpmb(kd, mt.rb_node.rbe_left);

    if (VerboseOpt)
	    printf("mbuf %p track %d\n", mt.m, mt.trackid);
    if (mt.trackid >= 0 && mt.trackid < 256)
	++tracks[mt.trackid];
    if (mt.m) {
	kkread(kd, (long)mt.m, &mb, sizeof(mb));
	if (mb.m_flags & M_EXT_CLUSTER)
		++count_cluster;
	else if (mb.m_flags & M_EXT)
		++count_ext;
	else
		++count_noncluster;
	count_data_bytes += mb.m_len;
	if (mb.m_flags & M_EXT) {
		count_buffer_space += mb.m_ext.ext_size;
	} else {
		count_buffer_space += MLEN;
	}
	if (VerboseOpt) {
		dumpmbdata(kd, mb.m_data, mb.m_len);
	}
    }

    if (mt.rb_node.rbe_right)
	dumpmb(kd, mt.rb_node.rbe_right);
}

static void
dumpmbdata(kvm_t *kd, char *data, int len)
{
    char buf[256];
    int i;
    int j;
    int n;
    int count;

    for (n = 0; n < len; n += count) {
	count = len - n;
	if (count > sizeof(buf))
		count = sizeof(buf);
	kkread(kd, (long)data + n, buf, count);
	for (i = 0; i < count; ++i) {
	    if ((n + i) % 16 == 0) {
		printf("    %04x ", n + i);
	    }
	    printf(" %02x", (unsigned char)buf[i]);
	    if ((n + i) % 16 == 15 || i + 1 == count) {
		printf(" ");
		for (j = i & ~15; j <= i; ++j)
		    printf("%c", isprint(buf[j]) ? buf[j] : '.');
		printf("\n");
	    }
	}
	printf("\n");
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
