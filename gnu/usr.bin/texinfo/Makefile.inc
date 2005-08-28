# $FreeBSD: src/gnu/usr.bin/texinfo/Makefile.inc,v 1.11 2000/01/17 10:59:13 ru Exp $
# $DragonFly: src/gnu/usr.bin/texinfo/Makefile.inc,v 1.3 2005/08/28 12:25:04 asmodai Exp $

TOPLVLDIR:=	${.CURDIR}/..
TXIDIR=		${.CURDIR}/../../../../contrib/texinfo-4

.if exists(${.OBJDIR}/../libtxi)
LIBTXI=		${.OBJDIR}/../libtxi/libtxi.a
.else
LIBTXI=		${.CURDIR}/../libtxi/libtxi.a
.endif

CFLAGS+=	-DHAVE_CONFIG_H -DLOCALEDIR=\"/usr/share/locale\" \
		-I${TOPLVLDIR} -I${TXIDIR} -I${TXIDIR}/lib

.include "../Makefile.inc"
