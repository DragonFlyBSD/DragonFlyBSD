#	@(#)Makefile	8.6 (Berkeley) 5/8/95
# $FreeBSD: src/sbin/mount/Makefile,v 1.9.2.1 2001/04/25 10:58:33 ru Exp $
# $DragonFly: src/sbin/mount/Makefile,v 1.3 2003/09/06 18:27:57 hmp Exp $

PROG=	mount
SRCS=	mount.c mount_ufs.c getmntopts.c vfslist.c
MAN=	mount.8
# We do NOT install the getmntopts.3 man page.

MLINKS+= mount.8 mount_ufs.8

.include <bsd.prog.mk>
