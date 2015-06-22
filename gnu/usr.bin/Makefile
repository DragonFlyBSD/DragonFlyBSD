# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $

.include "Makefile.buo"

SUBDIR= dialog diff grep groff man rcs

.if !defined(NO_CVS)
SUBDIR+=cvs
.endif

.if !defined(NO_GDB)
SUBDIR+=gdb
.endif

SUBDIR+= ${BU_PRIMARY}
.if !defined(NO_ALTBINUTILS)
SUBDIR+= ${BU_BACKUP}
.endif

SUBDIR+= gmp
SUBDIR+= mpfr
SUBDIR+= mpc
SUBDIR+= cc50

.if defined(NO_ALTCOMPILER)
.ORDER: ${BU_PRIMARY} gmp mpfr mpc cc50
.else
SUBDIR+= cc47
.ORDER: ${BU_PRIMARY} gmp mpfr mpc cc50 cc47
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
