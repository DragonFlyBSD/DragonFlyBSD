# $FreeBSD: src/sbin/mount_smbfs/Makefile,v 1.1.2.3 2001/12/20 16:28:30 sheldonh Exp $
# $DragonFly: src/sbin/mount_smbfs/Makefile,v 1.4 2005/02/28 13:57:40 joerg Exp $

PROG=	mount_smbfs
SRCS=	mount_smbfs.c getmntopts.c
WARNS?=	6
MAN=	mount_smbfs.8

MOUNTDIR=	${.CURDIR}/../mount
CONTRIBDIR=	${.CURDIR}/../../contrib/smbfs
CFLAGS+=	-DSMBFS -I${MOUNTDIR} -I${CONTRIBDIR}/include

LDADD+= -lsmb
DPADD+= ${LIBSMB}

# Needs to be dynamically linked for optional dlopen() access to
# userland libiconv (see the -E option).
#
NOSHARED?=	NO

.PATH:	${CONTRIBDIR}/mount_smbfs

.include <bsd.prog.mk>
