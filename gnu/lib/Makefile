# $FreeBSD: src/gnu/lib/Makefile,v 1.25.2.4 2001/01/06 23:16:53 obrien Exp $
# $DragonFly: src/gnu/lib/Makefile,v 1.12 2006/09/27 12:10:33 corecode Exp $

SUBDIR= libdialog libregex libreadline gcc34

.if defined(WANT_GCC40)
SUBDIR+= gcc40
.endif

.if defined(WANT_GCC41)
SUBDIR+= gcc41
.endif

.include <bsd.subdir.mk>
