# $FreeBSD: src/usr.sbin/mrouted/Makefile.inc,v 1.4 1999/08/28 01:17:02 peter Exp $
# $DragonFly: src/usr.sbin/mrouted/Makefile.inc,v 1.2 2003/06/17 04:29:57 dillon Exp $

CFLAGS+=	-DRSRR

.if exists(${.OBJDIR}/../common)
LIBMROUTED=	${.OBJDIR}/../common/libmrouted.a
.else
LIBMROUTED=	${.CURDIR}/../common/libmrouted.a
.endif

.include "../Makefile.inc"
