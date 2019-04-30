#	$NetBSD: Makefile,v 1.7 2009/12/01 23:12:51 haad Exp $

PROG=		lvm

LVM2_DIR=	${.CURDIR}/../../../contrib/lvm2/dist
LIBDM_DIR=	${LVM2_DIR}/libdm

.PATH:		${LVM2_DIR}/tools
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

CFLAGS+=	-I${LVM2_DIR}/lib -I${LVM2_DIR}/include \
		-I. -I${LVM2_DIR}/tools -I${LIBDM_DIR}
CFLAGS+=	-DLVM_SHARED_PATH=\"${BINDIR}/lvm\"

LDADD+=		-llvm -ldevmapper -lprop -ldevattr
DPADD+=		${LIBLVM} ${LIBDEVMAPPER} ${LIBPROP} ${LIBDEVATTR}

# Some parts of the code can't be protected because if its
# alloca() usage.
#
.if defined(USE_SSP)
CFLAGS+=	-Wno-stack-protector
.endif

# Do not build readline support for the rescue version.
#
.if !defined(RESCUE)
CFLAGS+=	-DREADLINE_SUPPORT
# libedit api change for lvm.c
CFLAGS+=	-DCPPFunction=rl_completion_func_t
CFLAGS+=	${PRIVATELIB_CFLAGS}
DPADD+=		${LIBEDIT} ${LIBNCURSES}
LDADD+=		-lprivate_edit -lprivate_ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
.endif

.PATH:		${LVM2_DIR}/man
MAN= 		lvchange.8 lvconvert.8 lvcreate.8 lvdisplay.8 lvextend.8 \
		lvm.8 lvm.conf.5 lvmchange.8 lvmdiskscan.8 lvmdump.8 \
		lvreduce.8 lvremove.8 lvrename.8 lvresize.8 lvs.8 \
		lvscan.8 pvchange.8 pvck.8 pvcreate.8 pvdisplay.8 \
		pvmove.8 pvremove.8 pvresize.8 pvs.8 pvscan.8 \
		vgcfgbackup.8 vgcfgrestore.8 vgchange.8 vgck.8 vgconvert.8 \
		vgcreate.8 vgdisplay.8 vgexport.8 vgextend.8 vgimport.8 \
		vgmerge.8 vgmknodes.8 vgreduce.8 vgremove.8 vgrename.8 \
		vgs.8 vgscan.8 vgsplit.8

SYMLINKS+=	lvm ${BINDIR}/lvchange \
		lvm ${BINDIR}/lvconvert \
		lvm ${BINDIR}/lvcreate \
		lvm ${BINDIR}/lvdisplay \
		lvm ${BINDIR}/lvextend \
		lvm ${BINDIR}/lvmchange \
		lvm ${BINDIR}/lvmdiskscan \
		lvm ${BINDIR}/lvmdump \
		lvm ${BINDIR}/lvreduce \
		lvm ${BINDIR}/lvremove \
		lvm ${BINDIR}/lvrename \
		lvm ${BINDIR}/lvresize \
		lvm ${BINDIR}/lvs \
		lvm ${BINDIR}/lvscan \
		lvm ${BINDIR}/pvchange \
		lvm ${BINDIR}/pvck \
		lvm ${BINDIR}/pvcreate \
		lvm ${BINDIR}/pvdisplay \
		lvm ${BINDIR}/pvmove \
		lvm ${BINDIR}/pvremove \
		lvm ${BINDIR}/pvresize \
		lvm ${BINDIR}/pvs \
		lvm ${BINDIR}/pvscan \
		lvm ${BINDIR}/vgcfgbackup \
		lvm ${BINDIR}/vgcfgrestore \
		lvm ${BINDIR}/vgchange \
		lvm ${BINDIR}/vgck \
		lvm ${BINDIR}/vgconvert \
		lvm ${BINDIR}/vgcreate \
		lvm ${BINDIR}/vgdisplay \
		lvm ${BINDIR}/vgexport \
		lvm ${BINDIR}/vgextend \
		lvm ${BINDIR}/vgimport \
		lvm ${BINDIR}/vgmerge \
		lvm ${BINDIR}/vgmknodes \
		lvm ${BINDIR}/vgreduce \
		lvm ${BINDIR}/vgremove \
		lvm ${BINDIR}/vgrename \
		lvm ${BINDIR}/vgs \
		lvm ${BINDIR}/vgscan \
		lvm ${BINDIR}/vgsplit

.include <bsd.prog.mk>
