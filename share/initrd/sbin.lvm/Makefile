#	$NetBSD: Makefile,v 1.7 2009/12/01 23:12:51 haad Exp $

BINDIR=		/usr/share/initrd/sbin
NOSHARED=	yes
NO_MAN=		yes

LVM2_DIR=	${.CURDIR}/../../../contrib/lvm2
LVM2_DISTDIR=       ${LVM2_DIR}/dist
LIBDM_DISTDIR=        ${LVM2_DIR}/dist/libdm
LIBDM_SRCDIR= ${LVM2_DIR}/lib/libdevmapper

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


CFLAGS+=	-D_XOPEN_SOURCE=600
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

LDADD+=		-llvm -ldevmapper -lprop -ledit -ltermcap -ldevattr
DPADD+=		${LIBLVM} ${LIBDEVMAPPER} ${LIBPROP} ${LIBEDIT} ${LIBTERMCAP}
DPADD+=		${LIBDEVATTR}

.PATH:	${LVM2_DISTDIR}/tools

.include <bsd.prog.mk>
