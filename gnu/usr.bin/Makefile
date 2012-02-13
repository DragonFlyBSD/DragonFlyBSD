# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $

.include "Makefile.buo"

SUBDIR= dialog diff grep groff man rcs texinfo

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= ${BU_PRIMARY}
.if !defined(NO_BINUTILS221)
SUBDIR+= ${BU_BACKUP}
.endif

.if !defined(NO_GCC41)
SUBDIR+= cc41
.ORDER: ${BU_PRIMARY} cc41
.endif

SUBDIR+= cc44
.ORDER: ${BU_PRIMARY} cc44

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
