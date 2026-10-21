# $FreeBSD: src/gnu/usr.bin/Makefile,v 1.51 2000/01/16 00:11:34 obrien Exp $

.include "Makefile.buo"

SUBDIR= dialog grep rcs

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

# gcc120 is now the default, see sys.mk for WORLD_ALTCOMPILER default
#
SUBDIR+= cc120
_compiler_dir= cc120

.if !defined(NO_ALTCOMPILER) && defined(WORLD_ALTCOMPILER)
. if ${WORLD_ALTCOMPILER} == "all" || ${WORLD_ALTCOMPILER:Mgcc80}
SUBDIR+= cc80
_altcompiler_dirs+= cc80
. endif
.endif

.ORDER: ${BU_PRIMARY} gmp mpfr mpc ${_compiler_dir} ${_altcompiler_dirs}

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
