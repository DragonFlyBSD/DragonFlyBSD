# $FreeBSD: src/usr.sbin/pkg_install/Makefile.inc,v 1.5 1999/08/28 01:17:53 peter Exp $
# $DragonFly: src/usr.sbin/pkg_install/Attic/Makefile.inc,v 1.2 2003/06/17 04:29:59 dillon Exp $

.if exists(${.OBJDIR}/../lib)
LIBINSTALL=	${.OBJDIR}/../lib/libinstall.a
.else
LIBINSTALL=	${.CURDIR}/../lib/libinstall.a
.endif

# Inherit BINDIR from one level up.
.include "../Makefile.inc"
