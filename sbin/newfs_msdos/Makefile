# $FreeBSD: src/sbin/newfs_msdos/Makefile,v 1.2.2.2 2001/04/25 10:58:42 ru Exp $
# $DragonFly: src/sbin/newfs_msdos/Makefile,v 1.2 2003/06/17 04:27:34 dillon Exp $

PROG=	newfs_msdos
MAN=	newfs_msdos.8

.if ${MACHINE} == "pc98"
CFLAGS+= -DPC98
.endif

.include <bsd.prog.mk>
