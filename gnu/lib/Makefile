# $FreeBSD: src/gnu/lib/Makefile,v 1.25.2.4 2001/01/06 23:16:53 obrien Exp $

SUBDIR= gcc44 libcryptsetup libdevmapper libdialog libluks liblvm

.if !defined(NO_GCC41)
SUBDIR+= gcc41

# FIXME: libobjc in gcc41 and gcc44 can collide installing header files
#
SUBDIR_ORDERED= gcc41 gcc44
.endif


.include <bsd.subdir.mk>
