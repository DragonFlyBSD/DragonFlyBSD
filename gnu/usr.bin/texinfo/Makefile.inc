# $FreeBSD: src/gnu/usr.bin/texinfo/Makefile.inc,v 1.11 2000/01/17 10:59:13 ru Exp $
# $DragonFly: src/gnu/usr.bin/texinfo/Makefile.inc,v 1.2 2003/06/17 04:25:49 dillon Exp $

TXIDIR=		${.CURDIR}/../../../../contrib/texinfo

.if exists(${.OBJDIR}/../libtxi)
LIBTXI=		${.OBJDIR}/../libtxi/libtxi.a
.else
LIBTXI=		${.CURDIR}/../libtxi/libtxi.a
.endif

CFLAGS+=	-DHAVE_CONFIG_H -DLOCALEDIR=\"/usr/share/locale\" \
		-I${TXIDIR} -I${TXIDIR}/lib

.include "../Makefile.inc"
