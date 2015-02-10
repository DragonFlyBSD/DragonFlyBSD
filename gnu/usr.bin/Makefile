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
.if !defined(NO_ALTBINUTILS)
SUBDIR+= ${BU_BACKUP}
.endif

SUBDIR+= gmp
SUBDIR+= mpfr
SUBDIR+= mpc
SUBDIR+= cc47

.if defined(NO_GCC50)
.ORDER: ${BU_PRIMARY} gmp mpfr mpc cc47
.else
SUBDIR+= cc50
.ORDER: ${BU_PRIMARY} gmp mpfr mpc cc47 cc50
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
