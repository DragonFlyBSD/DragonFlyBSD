# $FreeBSD: src/gnu/lib/Makefile,v 1.25.2.4 2001/01/06 23:16:53 obrien Exp $
# $DragonFly: src/gnu/lib/Makefile,v 1.17 2008/06/27 08:08:25 corecode Exp $

SUBDIR= libdialog libregex gcc41
.if !defined(NO_GCC44)
SUBDIR+= gcc44
.endif
SUBDIR+= libdevmapper liblvm

.include <bsd.subdir.mk>
