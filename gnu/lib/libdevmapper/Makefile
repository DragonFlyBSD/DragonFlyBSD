#	$NetBSD: Makefile,v 1.4 2009/12/09 00:17:23 haad Exp $

LVM2_DIR=	${.CURDIR}/../../../contrib/lvm2

LIBDM_DISTDIR=	${LVM2_DIR}/dist/libdm
LVM2_DISTDIR=	${LVM2_DIR}/dist

LIB=		devmapper
SHLIB_MAJOR?=	0

CSTD?=		gnu89

CFLAGS+=	-I${LIBDM_DISTDIR} -I${LIBDM_DISTDIR}/misc -I${LIBDM_DISTDIR}/mm \
		-I${LIBDM_DISTDIR}/datastruct -I${LIBDM_DISTDIR}/ioctl \
		-I${LVM2_DISTDIR}/include \
		-I${.CURDIR}/../../../sys/dev/disk/dm

CFLAGS+=	-D__LIB_DEVMAPPER__ -DDM_DEVICE_UID=0 \
		-DDM_DEVICE_GID=0 -DDM_DEVICE_MODE=0600

INCS+=		${LIBDM_DISTDIR}/libdevmapper.h

SRCS+=		bitset.c hash.c list.c libdm-common.c libdm-file.c \
		libdm-deptree.c	libdm-string.c libdm-report.c \
		dbg_malloc.c pool.c matcher.c parse_rx.c ttree.c \
		libdm-dragonfly-iface.c libdm_dragonfly.c

.PATH: ${LIBDM_DISTDIR}/
.PATH: ${LIBDM_DISTDIR}/datastruct
.PATH: ${LIBDM_DISTDIR}/mm
.PATH: ${LIBDM_DISTDIR}/regex
.PATH: ${LIBDM_DISTDIR}/ioctl

.include <bsd.lib.mk>
