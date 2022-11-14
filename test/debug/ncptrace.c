/*
 * NCPTRACE.C
 *
 * cc -I/usr/src/sys ncptrace.c -o /usr/local/bin/ncptrace -lkvm
 *
 * ncptrace
 * ncptrace [path]
 *
 * Trace and dump the kernel namecache hierarchy.  If a path is specified
 * the trace begins there, otherwise the trace begins at the root.
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
 * $DragonFly: src/test/debug/ncptrace.c,v 1.7 2007/05/06 20:45:01 dillon Exp $
 */

#define _KERNEL_STRUCTURES
#include <sys/param.h>
#include <sys/user.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/namecache.h>
#include <sys/mount.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>
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
    { "_rootnch" },
    { "_mountlist" },
    { NULL }
};

static char *getncppath(kvm_t *kd, struct nchandle *nch, char *buf, int bytes);
static int printvfc(kvm_t *kd, struct vfsconf *vfc);
static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);
static void dumpncp(kvm_t *kd, int isnull, int tab, struct namecache *,
			const char *);

static long num_unres;
static long num_leafs;
static long num_neg;
static long num_cache;

int
main(int ac, char **av)
{
    struct nchandle nch;
    struct mount mntinfo;
    struct mount *mntptr;
    struct mntlist list;
    kvm_t *kd;
    const char *corefile = NULL;
    const char *sysfile = NULL;
    const char *path;
    int ch;
    int i;
    int n;
    int isnull;
    char mntpath[1024];

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
    kkread(kd, Nl[0].n_value, &nch, sizeof(nch));
    kkread(kd, Nl[1].n_value, &list, sizeof(list));

    mntptr = TAILQ_FIRST(&list);
    while (mntptr) {
	kkread(kd, (long)mntptr, &mntinfo, sizeof(mntinfo));
	printf("MOUNT %p ", mntptr);
	if (mntinfo.mnt_vfc) {
	    isnull = printvfc(kd, mntinfo.mnt_vfc);
	    printf(" ");
	} else {
	    isnull = 0;
	}
	mntpath[sizeof(mntpath)-1] = 0;
	path = getncppath(kd, &mntinfo.mnt_ncmounton,
			  mntpath, sizeof(mntpath) - 1);
	printf("ON %s\n", path);
	if (ac == 0) {
	    dumpncp(kd, isnull, 0, mntinfo.mnt_ncmountpt.ncp, NULL);
	} else {
	    n = strlen(path);
	    for (i = 0; i < ac; ++i) {
		if (strncmp(path, av[i], n) == 0) {
		    dumpncp(kd, isnull, 0,
			    mntinfo.mnt_ncmountpt.ncp, av[i] + n);
		}
	    }
	}
	mntptr = TAILQ_NEXT(&mntinfo, mnt_list);
    }

    printf("numunres = %ld\n", num_unres);
    printf("numleafs = %ld\n", num_leafs);
    printf("numcache = %ld\n", num_cache);
    printf("numneg   = %ld\n", num_neg);
}

static void
dumpncp(kvm_t *kd, int isnull, int tab,
	struct namecache *ncptr, const char *path)
{
    struct namecache ncp;
    struct namecache *ncscan;
    const char *ptr;
    int haschildren;
    char name[256];

    kkread(kd, (u_long)ncptr, &ncp, sizeof(ncp));
    if (ncp.nc_nlen < sizeof(name)) {
	kkread(kd, (u_long)ncp.nc_name, name, ncp.nc_nlen);
	name[ncp.nc_nlen] = 0;
    } else {
	name[0] = 0;
    }

    if (isnull == 0) {
	if (ncp.nc_parent) {
	    if ((ncp.nc_flag & NCF_UNRESOLVED) && ncp.nc_list.tqh_first == NULL)
		++num_unres;
	    if (ncp.nc_list.tqh_first == NULL)
		++num_leafs;
	    ++num_cache;
	}
	if ((ncp.nc_flag & NCF_UNRESOLVED) == 0 && ncp.nc_vp == NULL)
	    ++num_neg;
    }

    if (tab == 0) {
	strcpy(name, "FSROOT");
	if (path && *path == '/')
	    ++path;
    } else if (name[0] == 0) {
	strcpy(name, "?");
	if (path)
	    return;
    } else if (path) {
	if ((ptr = strchr(path, '/')) == NULL)
	    ptr = path + strlen(path);
	if (strlen(name) != ptr - path ||
	    bcmp(name, path, ptr - path) != 0
	) {
	    return;
	}
	path = ptr;
	if (*path == '/')
	    ++path;
    }
    if (path && *path == 0)
	path = NULL;

    if (ncp.nc_list.tqh_first)
	haschildren = 1;
    else
	haschildren = 0;

    if (path)
	printf("ELM ");
    else
	printf("%*.*s%s ", tab, tab, "", name);
    printf("[ncp=%p par=%p %04x vp=%p", 
	    ncptr, ncp.nc_parent, ncp.nc_flag, ncp.nc_vp);
    if (ncp.nc_timeout)
	printf(" timo=%d", ncp.nc_timeout);
    if (ncp.nc_refs)
	printf(" refs=%d", ncp.nc_refs);
    if (ncp.nc_generation)
	printf(" gen=%d", ncp.nc_generation);
    if ((ncp.nc_flag & NCF_UNRESOLVED) == 0 && ncp.nc_error)
	printf(" error=%d", ncp.nc_error);
    if (ncp.nc_flag & NCF_ISMOUNTPT)
	printf(" MAYBEMOUNT");
    if (ncp.nc_lock.lk_count & ~LKC_SHARED) {
	printf(" LOCKSTATUS(%016lx,td=%p)",
	       ncp.nc_lock.lk_count, ncp.nc_lock.lk_lockholder);
    }
    printf("]");

    if (path) {
	printf(" %s\n", name);
    } else {
	printf("%s\n", haschildren ? " {" : "");
    }
    for (ncscan = ncp.nc_list.tqh_first; ncscan; ncscan = ncp.nc_entry.tqe_next) {
	kkread(kd, (u_long)ncscan, &ncp, sizeof(ncp));
	dumpncp(kd, isnull, (path ? (tab ? tab : 4) : tab + 4), ncscan, path);
    }
    if (haschildren && path == NULL)
	printf("%*.*s}\n", tab, tab, "");
}

static
char *
getncppath(kvm_t *kd, struct nchandle *nch, char *base, int bytes)
{
    struct mount mntinfo;
    struct namecache ncp;
    struct namecache *ncpptr;

    ncpptr = nch->ncp;
    while (ncpptr) {
	kkread(kd, (long)ncpptr, &ncp, sizeof(ncp));
	if (ncp.nc_nlen >= bytes)
	    break;
	kkread(kd, (long)ncp.nc_name, base + bytes - ncp.nc_nlen, ncp.nc_nlen);
	bytes -= ncp.nc_nlen;
	if (ncp.nc_parent) {
	    base[--bytes] = '/';
	}
	ncpptr = ncp.nc_parent;
    }
    if (nch->mount) {
	kkread(kd, (long)nch->mount, &mntinfo, sizeof(mntinfo));
	if (mntinfo.mnt_ncmounton.mount)
	    return(getncppath(kd, &mntinfo.mnt_ncmounton, base, bytes));
    } else if (base[bytes] == 0) {
	base[--bytes] = '/';
    }
    return(base + bytes);
}

static
int
printvfc(kvm_t *kd, struct vfsconf *vfc)
{
    struct vfsconf vfcinfo;

    kkread(kd, (long)vfc, &vfcinfo, sizeof(vfcinfo));
    printf("%s [type %d]", vfcinfo.vfc_name, vfcinfo.vfc_typenum);

    return (strcmp(vfcinfo.vfc_name, "null") == 0);
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}

