/*
 * VNODEINFO.C
 *
 * cc -I/usr/src/sys vnodeinfo.c -o /usr/local/bin/vnodeinfo -lkvm
 *
 * vnodeinfo
 *
 * Dump the mountlist and related vnodes.
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
 * $DragonFly: src/test/debug/vnodeinfo.c,v 1.13 2007/05/06 20:45:01 dillon Exp $
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

struct nlist Nl[] = {
    { "_mountlist" },
    { "_vnode_inactive_list" },
    { "_vnode_active_list" },
    { NULL }
};

static void kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes);
static struct mount *dumpmount(kvm_t *kd, struct mount *mp);
static struct vnode *dumpvp(kvm_t *kd, struct vnode *vp, int whichlist);
static void dumpbufs(kvm_t *kd, void *bufp, const char *id);
static void dumplocks(kvm_t *kd, struct lockf *lockf);
static void dumplockinfo(kvm_t *kd, struct lockf_range *item);
static int getobjpages(kvm_t *kd, struct vm_object *obj);
static int getobjvnpsize(kvm_t *kd, struct vm_object *obj);

int tracebufs = 0;
int tracelocks = 0;
int withnames = 0;

int
main(int ac, char **av)
{
    struct mount *mp;
    struct vnode *vp;
    kvm_t *kd;
    int i;
    int ch;
    const char *corefile = NULL;
    const char *sysfile = NULL;

    while ((ch = getopt(ac, av, "alnbM:N:")) != -1) {
	switch(ch) {   
	case 'b':
	    tracebufs = 1;
	    break;
	case 'n':
	    withnames = 1;
	    break;
	case 'l':
	    tracelocks = 1;
	    break;
	case 'a':
	    tracebufs = 1;
	    tracelocks = 1;
	    withnames = 1;
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

    if ((kd = kvm_open(sysfile, corefile, NULL, O_RDONLY, "kvm:")) == NULL) {
	perror("kvm_open");
	exit(1);
    }
    if (kvm_nlist(kd, Nl) != 0) {
	perror("kvm_nlist");
	exit(1);
    }
    kkread(kd, Nl[0].n_value, &mp, sizeof(mp));
    while (mp)
	mp = dumpmount(kd, mp);
    printf("INACTIVELIST {\n");
    kkread(kd, Nl[1].n_value, &vp, sizeof(vp));
    while (vp)
	vp = dumpvp(kd, vp, 0);
    printf("}\n");
    printf("ACTIVELIST {\n");
    kkread(kd, Nl[2].n_value, &vp, sizeof(vp));
    while (vp)
	vp = dumpvp(kd, vp, 0);
    printf("}\n");
    return(0);
}

static struct mount *
dumpmount(kvm_t *kd, struct mount *mp)
{
    struct mount mnt;
    struct vnode *vp;

    kkread(kd, (u_long)mp, &mnt, sizeof(mnt));
    printf("MOUNTPOINT %s on %s {\n", 
	mnt.mnt_stat.f_mntfromname, mnt.mnt_stat.f_mntonname);
    printf("    lk_flags %08x share %d wait %d excl %d holder = %p\n",
	mnt.mnt_lock.lk_flags, mnt.mnt_lock.lk_sharecount,
	mnt.mnt_lock.lk_waitcount, mnt.mnt_lock.lk_exclusivecount,
	mnt.mnt_lock.lk_lockholder);
    printf("    mnt_flag %08x mnt_kern_flag %08x\n", 
	mnt.mnt_flag, mnt.mnt_kern_flag);
    printf("    mnt_nvnodelistsize %d\n", mnt.mnt_nvnodelistsize);
    printf("    mnt_stat.f_fsid %08x %08x\n", mnt.mnt_stat.f_fsid.val[0],
	mnt.mnt_stat.f_fsid.val[1]);
    vp = mnt.mnt_nvnodelist.tqh_first;
    while (vp)
	vp = dumpvp(kd, vp, 1);

    printf("}\n");

    return(mnt.mnt_list.tqe_next);
}

static const char *
vtype(enum vtype type)
{
    static char buf[32];

    switch(type) {
    case VNON:
	return("VNON");
    case VREG:
	return("VREG");
    case VDIR:
	return("VDIR");
    case VBLK:
	return("VBLK");
    case VCHR:
	return("VCHR");
    case VLNK:
	return("VLNK");
    case VSOCK:
	return("VSOCK");
    case VFIFO:
	return("VFIFO");
    case VBAD:
	return("VBAD");
    default:
	break;
    }
    snprintf(buf, sizeof(buf), "%d", (int)type);
    return(buf);
}

static struct vnode *
dumpvp(kvm_t *kd, struct vnode *vp, int whichlist)
{
    struct vnode vn;

    kkread(kd, (u_long)vp, &vn, sizeof(vn));

    printf("    vnode %p.%d refcnt %08x auxcnt %d type=%s flags %08x",
	vp, vn.v_state, vn.v_refcnt, vn.v_auxrefs, vtype(vn.v_type), vn.v_flag);

    if ((vn.v_flag & VOBJBUF) && vn.v_object) {
	int npages = getobjpages(kd, vn.v_object);
	int vnpsize = getobjvnpsize(kd, vn.v_object);
	if (npages || vnpsize)
	    printf(" vmobjpgs=%d vnpsize=%d", npages, vnpsize);
    }

    if (vn.v_flag & VROOT)
	printf(" VROOT");
    if (vn.v_flag & VTEXT)
	printf(" VTEXT");
    if (vn.v_flag & VSYSTEM)
	printf(" VSYSTEM");
    if (vn.v_flag & VISTTY)
	printf(" VISTTY");
#ifdef VXLOCK
    if (vn.v_flag & VXLOCK)
	printf(" VXLOCK");
    if (vn.v_flag & VXWANT)
	printf(" VXWANT");
#endif
#ifdef VRECLAIMED
    if (vn.v_flag & VRECLAIMED)
	printf(" VRECLAIMED");
    if (vn.v_flag & VINACTIVE)
	printf(" VINACTIVE");
#endif
    if (vn.v_flag & VOBJBUF)
	printf(" VOBJBUF");
#ifdef VSWAPCACHE
    if (vn.v_flag & VSWAPCACHE)
	printf(" VSWAPCACHE");
#endif
    switch(vn.v_flag & (VAGE0 | VAGE1)) {
    case 0:
	printf(" VAGE0");
	break;
    case VAGE0:
	printf(" VAGE1");
	break;
    case VAGE1:
	printf(" VAGE2");
	break;
    case VAGE0 | VAGE1:
	printf(" VAGE3");
	break;
    }
    if (vn.v_flag & VOLOCK)
	printf(" VOLOCK");
    if (vn.v_flag & VOWANT)
	printf(" VOWANT");
#ifdef VDOOMED
    if (vn.v_flag & VDOOMED)
	printf(" VDOOMED");
#endif
#ifdef VINFREE
    if (vn.v_flag & VINFREE)
	printf(" VINFREE");
#endif
    if (vn.v_flag & VONWORKLST)
	printf(" VONWORKLST");
    if (vn.v_flag & VOBJDIRTY)
	printf(" VOBJDIRTY");
    if (vn.v_flag & VMAYHAVELOCKS)
	printf(" VMAYHAVELOCKS");

    printf("\n");

    if (vn.v_lock.lk_sharecount || vn.v_lock.lk_waitcount || 
	vn.v_lock.lk_exclusivecount || vn.v_lock.lk_lockholder != LK_NOTHREAD) {
	printf("\tlk_flags %08x share %d wait %d excl %d holder = %p\n",
	    vn.v_lock.lk_flags, vn.v_lock.lk_sharecount,
	    vn.v_lock.lk_waitcount, vn.v_lock.lk_exclusivecount,
	    vn.v_lock.lk_lockholder);
    }

    if (withnames && TAILQ_FIRST(&vn.v_namecache)) {
	struct namecache ncp;
	int nlen;
	char buf[1024];

	kkread(kd, (u_long)TAILQ_FIRST(&vn.v_namecache), &ncp, sizeof(ncp));
	if ((nlen = ncp.nc_nlen) >= sizeof(buf))
		nlen = sizeof(buf) - 1;
	if (nlen < 0)
		nlen = 0;
	if (nlen) {
		kkread(kd, (u_long)ncp.nc_name, buf, nlen);
		buf[nlen] = 0;
		printf("\tfilename %s\n", buf);
	}
    }

    if (tracebufs) {
	if (vn.v_rbclean_tree.rbh_root) {
	    printf("\tCLEAN BUFFERS\n");
	    dumpbufs(kd, vn.v_rbclean_tree.rbh_root, "ROOT");
	}
	if (vn.v_rbdirty_tree.rbh_root) {
	    printf("\tDIRTY BUFFERS\n");
	    dumpbufs(kd, vn.v_rbdirty_tree.rbh_root, "ROOT");
	}
    }

    if (tracelocks) {
	if (vn.v_tag == VT_UFS && vn.v_data) {
	    struct inode *ip = vn.v_data;
	    struct lockf lockf;

	    kkread(kd, (u_long)&ip->i_lockf, &lockf, sizeof(lockf));
	    dumplocks(kd, &lockf);
	}
    }


    if (whichlist)
	return(vn.v_nmntvnodes.tqe_next);
    else
	return(vn.v_list.tqe_next);
}

static void
dumpbufs(kvm_t *kd, void *bufp, const char *id)
{
	struct buf buf;

	kkread(kd, (u_long)bufp, &buf, sizeof(buf));
	printf("\t    %-8s %p loffset %012llx/%05x foffset %08llx",
		id, bufp,
		buf.b_bio1.bio_offset,
		buf.b_bufsize,
		buf.b_bio2.bio_offset);
	printf(" q=%d lck=%d/%d flags=%08x refs=%d dep=%p",
		buf.b_qindex, buf.b_lock.lk_sharecount,
		buf.b_lock.lk_exclusivecount,
		buf.b_flags, buf.b_refs, buf.b_dep.lh_first);
	printf("\n");

	if (buf.b_rbnode.rbe_left)
	    dumpbufs(kd, buf.b_rbnode.rbe_left, "LEFT");
	if (buf.b_rbnode.rbe_right)
	    dumpbufs(kd, buf.b_rbnode.rbe_right, "RIGHT");
}

static void
dumplocks(kvm_t *kd, struct lockf *lockf)
{
	struct lockf_range item;
	struct lockf_range *scan;

	if ((scan = TAILQ_FIRST(&lockf->lf_range)) != NULL) {
		printf("\tLOCKS\n");
		do {
			kkread(kd, (u_long)scan, &item, sizeof(item));
			dumplockinfo(kd, &item);
		} while ((scan = TAILQ_NEXT(&item, lf_link)) != NULL);
		printf("\n");
	}
	if ((scan = TAILQ_FIRST(&lockf->lf_blocked)) != NULL) {
		printf("\tBLKED\n");
		do {
			kkread(kd, (u_long)scan, &item, sizeof(item));
			dumplockinfo(kd, &item);
		} while ((scan = TAILQ_NEXT(&item, lf_link)) != NULL);
		printf("\n");
	}

}

static void
dumplockinfo(kvm_t *kd, struct lockf_range *item)
{
	int ownerpid;

	if (item->lf_owner && (item->lf_flags & F_POSIX)) {
		kkread(kd, (u_long)&item->lf_owner->p_pid,
			&ownerpid, sizeof(ownerpid));
	} else {
		ownerpid = -1;
	}

	printf("\t    ty=%d flgs=%04x %lld-%lld owner=%d\n",
		item->lf_type, item->lf_flags,
		item->lf_start, item->lf_end,
		ownerpid
	);
}

static
int
getobjpages(kvm_t *kd, struct vm_object *obj)
{
	struct vm_object vmobj;

	kkread(kd, (u_long)obj, &vmobj, sizeof(vmobj));
	return(vmobj.resident_page_count);
}

static
int
getobjvnpsize(kvm_t *kd, struct vm_object *obj)
{
	struct vm_object vmobj;

	kkread(kd, (u_long)obj, &vmobj, sizeof(vmobj));
	return ((int)vmobj.size);
}

static void
kkread(kvm_t *kd, u_long addr, void *buf, size_t nbytes)
{
    if (kvm_read(kd, addr, buf, nbytes) != nbytes) {
        perror("kvm_read");
        exit(1);
    }
}

