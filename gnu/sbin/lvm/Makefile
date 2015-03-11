#	$NetBSD: Makefile,v 1.7 2009/12/01 23:12:51 haad Exp $

LVM2_DIR=	${.CURDIR}/../../../contrib/lvm2
LVM2_DISTDIR=       ${LVM2_DIR}/dist
LIBDM_DISTDIR=        ${LVM2_DIR}/dist/libdm
LIBDM_SRCDIR= ${LVM2_DIR}/lib/libdevmapper

.PATH:	${LVM2_DISTDIR}/man

PROG=		lvm

CSTD?=		gnu89

SRCS=		dumpconfig.c formats.c lvchange.c lvconvert.c lvcreate.c \
		lvdisplay.c lvextend.c lvmchange.c lvmcmdline.c \
		lvmdiskscan.c lvreduce.c lvremove.c lvrename.c lvresize.c \
		lvscan.c polldaemon.c pvchange.c pvck.c pvcreate.c \
		pvdisplay.c pvmove.c pvremove.c pvresize.c pvscan.c \
		reporter.c segtypes.c toollib.c vgcfgbackup.c \
		vgcfgrestore.c vgchange.c vgck.c vgconvert.c vgcreate.c \
		vgdisplay.c vgexport.c vgextend.c vgimport.c vgmerge.c \
		vgmknodes.c vgreduce.c vgremove.c vgrename.c vgscan.c \
		vgsplit.c lvm.c

CFLAGS+=	-I${LVM2_DISTDIR}/lib -I${LVM2_DISTDIR}/include \
		-I. -I${LVM2_DISTDIR}/tools \
		-I${LIBDM_DISTDIR}

CFLAGS+=	-DLVM_SHARED_PATH=\"$(BINDIR)/lvm\"

# Some parts of the code can't be protected because if its
# alloca() usage.
#
.if defined(USE_SSP) && (${USE_SSP} != "no")
CFLAGS+=      -Wno-stack-protector
.endif

MAN= 		lvchange.8 lvconvert.8 lvcreate.8 lvdisplay.8 lvextend.8 \
		lvm.8 lvm.conf.5 lvmchange.8 lvmdiskscan.8 lvmdump.8 \
		lvreduce.8 lvremove.8 lvrename.8 lvresize.8 lvs.8 \
		lvscan.8 pvchange.8 pvck.8 pvcreate.8 pvdisplay.8 \
		pvmove.8 pvremove.8 pvresize.8 pvs.8 pvscan.8 \
		vgcfgbackup.8 vgcfgrestore.8 vgchange.8 vgck.8 vgconvert.8 \
		vgcreate.8 vgdisplay.8 vgexport.8 vgextend.8 vgimport.8 \
		vgmerge.8 vgmknodes.8 vgreduce.8 vgremove.8 vgrename.8 \
		vgs.8 vgscan.8 vgsplit.8

SYMLINKS+=	${BINDIR}/lvm ${BINDIR}/lvchange \
		${BINDIR}/lvm ${BINDIR}/lvconvert \
		${BINDIR}/lvm ${BINDIR}/lvcreate \
		${BINDIR}/lvm ${BINDIR}/lvdisplay \
		${BINDIR}/lvm ${BINDIR}/lvextend \
		${BINDIR}/lvm ${BINDIR}/lvmchange \
		${BINDIR}/lvm ${BINDIR}/lvmdiskscan \
		${BINDIR}/lvm ${BINDIR}/lvmdump \
		${BINDIR}/lvm ${BINDIR}/lvreduce \
		${BINDIR}/lvm ${BINDIR}/lvremove \
		${BINDIR}/lvm ${BINDIR}/lvrename \
		${BINDIR}/lvm ${BINDIR}/lvresize \
		${BINDIR}/lvm ${BINDIR}/lvs \
		${BINDIR}/lvm ${BINDIR}/lvscan \
		${BINDIR}/lvm ${BINDIR}/pvchange \
		${BINDIR}/lvm ${BINDIR}/pvck \
		${BINDIR}/lvm ${BINDIR}/pvcreate \
		${BINDIR}/lvm ${BINDIR}/pvdisplay \
		${BINDIR}/lvm ${BINDIR}/pvmove \
		${BINDIR}/lvm ${BINDIR}/pvremove \
		${BINDIR}/lvm ${BINDIR}/pvresize \
		${BINDIR}/lvm ${BINDIR}/pvs \
		${BINDIR}/lvm ${BINDIR}/pvscan \
		${BINDIR}/lvm ${BINDIR}/vgcfgbackup \
		${BINDIR}/lvm ${BINDIR}/vgcfgrestore \
		${BINDIR}/lvm ${BINDIR}/vgchange \
		${BINDIR}/lvm ${BINDIR}/vgck \
		${BINDIR}/lvm ${BINDIR}/vgconvert \
		${BINDIR}/lvm ${BINDIR}/vgcreate \
		${BINDIR}/lvm ${BINDIR}/vgdisplay \
		${BINDIR}/lvm ${BINDIR}/vgexport \
		${BINDIR}/lvm ${BINDIR}/vgextend \
		${BINDIR}/lvm ${BINDIR}/vgimport \
		${BINDIR}/lvm ${BINDIR}/vgmerge \
		${BINDIR}/lvm ${BINDIR}/vgmknodes \
		${BINDIR}/lvm ${BINDIR}/vgreduce \
		${BINDIR}/lvm ${BINDIR}/vgremove \
		${BINDIR}/lvm ${BINDIR}/vgrename \
		${BINDIR}/lvm ${BINDIR}/vgs \
		${BINDIR}/lvm ${BINDIR}/vgscan \
		${BINDIR}/lvm ${BINDIR}/vgsplit

MKLINKS+=	lvchange.8 lvchange.1 \
                lvconvert.8 lvconvert.1 \
                lvcreate.8 lvcreate.1 \
                lvdisplay.8 lvdisplay.1 \
                lvextend.8 lvextend.1 \
                lvm.8 lvm.1 \
                lvm.conf lvm.1 \
                lvmchange.8 lvmchange.1 \
                lvmdiskscan.8 lvmdiskscan.1 \
                lvmdump.8 lvmdump.1 \
                lvreduce.8 lvreduce.1 \
                lvremove.8 lvremove.1 \
                lvrename.8 lvrename.1 \
                lvresize.8 lvresize.1 \
                lvs.8 lvs.1 \
                lvscan.8 lvscan.1 \
                pvchange.8 pvchange.1 \
                pvck.8 pvck.1 \
                pvcreate.8 pvcreate.1 \
                pvdisplay.8 pvdisplay.1 \
                pvmove.8 pvmove.1 \
                pvremove.8 pvremove.1 \
                pvresize.8 pvresize.1 \
                pvs.8 pvs.1 \
                pvscan.8 pvscan.1 \
                vgcfgbackup.8 vgcfgbackup.1 \
                vgcfgrestore.8 vgcfgrestore.1 \
                vgchange.8 vgchange.1 \
                vgck.8 vgck.1 \
                vgconvert.8 vgconvert.1 \
                vgcreate.8 vgcreate.1 \
                vgdisplay.8 vgdisplay.1 \
                vgexport.8 vgexport.1 \
                vgextend.8 vgextend.1 \
                vgimport.8 vgimport.1 \
                vgmerge.8 vgmerge.1 \
                vgmknodes.8 vgmknodes.1 \
                vgreduce.8 vgreduce.1 \
                vgremove.8 vgremove.1 \
                vgrename.8 vgrename.1 \
                vgs.8 vgs.1 \
                vgscan.8 vgscan.1 \
                vgsplit.8 vgsplit.1

LDADD+=		-llvm -ldevmapper -lprop -ledit -ltermcap -ldevattr
DPADD+=		${LIBLVM} ${LIBDEVMAPPER} ${LIBPROP} ${LIBEDIT} ${LIBTERMCAP}
DPADD+=		${LIBDEVATTR}

.PATH:	${LVM2_DISTDIR}/tools

.include <bsd.prog.mk>
