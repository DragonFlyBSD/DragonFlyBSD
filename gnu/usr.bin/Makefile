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

SUBDIR+= gmp
SUBDIR+= mpfr

.if !defined(NO_GCC47)
SUBDIR+= mpc
.endif

SUBDIR+= cc44

.if !defined(NO_GCC47)
SUBDIR+= cc47
.ORDER: ${BU_PRIMARY} gmp mpfr mpc cc44 cc47
.else
.ORDER: ${BU_PRIMARY} gmp mpfr cc44
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
