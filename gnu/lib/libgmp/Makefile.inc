#
# $FreeBSD: src/gnu/lib/libgmp/Makefile.inc,v 1.4 1999/08/27 23:32:50 peter Exp $
# $DragonFly: src/gnu/lib/libgmp/Attic/Makefile.inc,v 1.2 2003/06/17 04:25:44 dillon Exp $
#

SHLIB_MAJOR=	3
SHLIB_MINOR=	0

.if exists(${.OBJDIR})
CFLAGS+=	-I${.OBJDIR}
.else
CFLAGS+=	-I${.CURDIR}
.endif
