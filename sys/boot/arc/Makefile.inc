# $FreeBSD: src/sys/boot/arc/Makefile.inc,v 1.2 2000/05/01 20:26:21 peter Exp $
# $DragonFly: src/sys/boot/arc/Attic/Makefile.inc,v 1.3 2003/11/10 06:08:30 dillon Exp $
# Options used when building app-specific libalpha components
LOAD_ADDRESS=		0xffffffff80900000
DPADD+=			${DESTDIR}/${LIBDIR}/libstand.a
LIBSTANDDIR=		${.CURDIR}/../../../../lib/libstand
LIBSTAND=		-lstand
LIBARC=			${.OBJDIR}/../lib/libarc.a
