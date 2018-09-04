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

# gcc80 is now the default, see sys.mk for WORLD_ALTCOMPILER default
#
#
SUBDIR+= cc80
_compiler_dir= cc80

.if !defined(NO_ALTCOMPILER)
. if defined(WORLD_ALTCOMPILER) && (${WORLD_ALTCOMPILER} == "all" || ${WORLD_ALTCOMPILER:Mgcc47})
SUBDIR+= cc47
_altcompiler_dirs+= cc47
. endif
. if defined(WORLD_ALTCOMPILER) && (${WORLD_ALTCOMPILER} == "all" || ${WORLD_ALTCOMPILER:Mgcc50})
SUBDIR+= cc50
_altcompiler_dirs+= cc50
. endif
.endif

.ORDER: ${BU_PRIMARY} gmp mpfr mpc ${_compiler_dir} ${_altcompiler_dirs}

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
