# $FreeBSD: src/usr.sbin/lpr/Makefile.inc,v 1.5.2.2 2002/02/01 01:03:39 gad Exp $
# $DragonFly: src/usr.sbin/lpr/Makefile.inc,v 1.3 2008/10/29 18:18:04 swildner Exp $

CFLAGS+= -DINET6
WARNS?=	 2

.if exists(${.OBJDIR}/../common_source)
LIBLPR=	${.OBJDIR}/../common_source/liblpr.a
.else
LIBLPR=	${.CURDIR}/../common_source/liblpr.a
.endif

.include "../Makefile.inc"
