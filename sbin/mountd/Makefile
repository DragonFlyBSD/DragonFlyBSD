#	From: @(#)Makefile	8.3 (Berkeley) 1/25/94
# $FreeBSD: src/sbin/mountd/Makefile,v 1.9.2.1 2001/04/25 10:58:41 ru Exp $
# $DragonFly: src/sbin/mountd/Makefile,v 1.2 2003/06/17 04:27:33 dillon Exp $

PROG=	mountd
CFLAGS+=-DNFS -DMFS -DCD9660 -DMSDOSFS
MAN=	exports.5 netgroup.5 mountd.8

.include <bsd.prog.mk>
