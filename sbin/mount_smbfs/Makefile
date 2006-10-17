# $FreeBSD: src/sbin/mount_smbfs/Makefile,v 1.1.2.3 2001/12/20 16:28:30 sheldonh Exp $
# $DragonFly: src/sbin/mount_smbfs/Makefile,v 1.5 2006/10/17 00:55:42 pavalos Exp $

PROG=	mount_smbfs
SRCS=	mount_smbfs.c getmntopts.c
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
