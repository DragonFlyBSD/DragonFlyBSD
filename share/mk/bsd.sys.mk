# $FreeBSD: src/share/mk/bsd.sys.mk,v 1.3.2.5 2002/07/03 16:59:14 des Exp $
#
# This file contains common settings used for building DragonFly
# sources.

# Support handling -W flags for both host cc and target cc.
.if defined(__USE_HOST_CCVER)
_WCCVER=	${HOST_CCVER}
.else
_WCCVER=	${CCVER}
.endif

CSTD?=	gnu99

.if ${CSTD} == "k&r"
CFLAGS		+= -traditional
.elif ${CSTD} == "c89" || ${CSTD} == "c90"
CFLAGS		+= -std=iso9899:1990
.elif ${CSTD} == "c94" || ${CSTD} == "c95"
CFLAGS		+= -std=iso9899:199409
.elif ${CSTD} == "c99"
CFLAGS		+= -std=iso9899:1999
.else
CFLAGS		+= -std=${CSTD}
.endif

# Explicitly clear _cnowarnflags (should not be used in Makefiles).
_cnowarnflags=

# Enable various levels of compiler warning checks.  These may be
# overridden (e.g. if using a non-gcc compiler) by defining NO_WARNS.

.if !defined(NO_WARNS)
. if defined(WARNS)
.  if ${WARNS} >= 1
CWARNFLAGS	+=	-Wmissing-include-dirs -Wsystem-headers
.   if !defined(NO_WERROR) && (${_WCCVER} == "gcc47" || ${_WCCVER} == "gcc80")
CWARNFLAGS	+=	-Werror
.   endif
.  endif
.  if ${WARNS} >= 2
CWARNFLAGS	+=	-Wall -Wformat-security -Winit-self
.  endif
.  if ${WARNS} >= 3
CWARNFLAGS	+=	-Wextra -Wstrict-prototypes\
			-Wmissing-prototypes -Wpointer-arith\
			-Wold-style-definition
.  endif
.  if ${WARNS} >= 4
CWARNFLAGS	+=	-Wreturn-type -Wcast-qual -Wwrite-strings -Wswitch\
			-Wshadow -Wcast-align -Wunused-parameter
.  endif
.  if ${WARNS} >= 6
CWARNFLAGS	+=	-Wchar-subscripts -Winline -Wnested-externs\
			-Wredundant-decls
.  endif
.  if ${WARNS} >= 2 && ${WARNS} <= 6
# Delete -Wno-pointer-sign from -Wall by default (C only).
_cnowarnflags	+=	-Wno-pointer-sign
.  endif
.  if ${WARNS} >= 2 && ${WARNS} <= 4
# XXX Delete -Wmaybe-uninitialized by default for now -- the compiler doesn't
# XXX always get it right.
.   if ${_WCCVER:Mgcc*}
_cnowarnflags	+=	-Wno-maybe-uninitialized
.   else
_cnowarnflags	+=	-Wno-uninitialized
.   endif
.  endif
.  if ${WARNS} == 3
# Delete -Wno-unused-parameter from -Wextra by default only if WARNS < 4.
_cnowarnflags	+=	-Wno-unused-parameter
.  endif
# Delete -Wformat-* family that give little benefits, same for stringop.
.  if ${WARNS} >= 2 && ${WARNS} <= 6 && ${_WCCVER:Mgcc8*}
_cnowarnflags	+=	-Wno-format-overflow -Wno-format-truncation
_cnowarnflags	+=	-Wno-stringop-truncation
.  endif
.  if ${WARNS} >= 1 && ${WARNS} <= 6 && ${_WCCVER:Mgcc8*}
_cnowarnflags	+=	-Wno-stringop-overflow
.  endif
# Activate gcc47's -Wunused-but-set-variable (which is in -Wall) and
# -Wunused-but-set-parameter (which is in -Wextra) only at WARNS >= 4
# (which is the level when also -Wunused-parameter comes into play).
.  if ${WARNS} >= 2 && ${WARNS} <= 3 && ${_WCCVER:Mgcc*}
_cnowarnflags	+=	-Wno-unused-but-set-variable
.  endif
.  if ${WARNS} == 3 && ${_WCCVER:Mgcc*}
_cnowarnflags	+=	-Wno-unused-but-set-parameter
.  endif
.  if ${WARNS} == 3 && (${_WCCVER:Mgcc49} || ${_WCCVER:Mgcc[5-]*})
_cnowarnflags	+=	-Wno-unused-value
.  endif
.  if ${WARNS} == 3 && ${_WCCVER:Mgcc8*}
_cnowarnflags	+=	-Wno-implicit-fallthrough
.  endif
.  if ${WARNS} >= 2 && ${_WCCVER:Mgcc4[789]}
_cnowarnflags	+=	-Wno-error=maybe-uninitialized\
			-Wno-error=uninitialized\
			-Wno-error=shadow
.  endif
# Disable -Werror selectively for -Os and -Og compilations.  Both -Winline and
# -Wmaybe-uninitialized are noisy and should be caught by standard -O and -O2.
# These are still useful diagnostics while investigating compilation issues.
.  if defined(WORLD_CCOPTLEVEL) && (${WORLD_CCOPTLEVEL:Mg} || ${WORLD_CCOPTLEVEL:Ms})
.   if ${WARNS} >= 6
CWARNFLAGS	+=	-Wno-error=inline
.   endif
.   if ${WARNS} >= 5 && ${_WCCVER:Mgcc*}
CWARNFLAGS	+=	-Wno-error=maybe-uninitialized
.   endif
.  endif
. endif

. if defined(FORMAT_AUDIT)
WFORMAT		=	1
. endif
. if defined(WFORMAT)
.  if ${WFORMAT} > 0
CWARNFLAGS	+=	-Wformat=2
.   if !defined(NO_WERROR) && (${_WCCVER} == "gcc47" || ${_WCCVER} == "gcc80")
CWARNFLAGS	+=	-Werror
.   endif
.  endif
. endif
.endif

# Build world with -fno-common. This will be default with GCC 10.
#
.if ${_WCCVER:Ngcc1[0-9][0-9]}
CFLAGS		+=	-fno-common
.endif

.if defined(NO_WCAST_FUNCTION_TYPE) && ${WARNS} >= 3 && ${_WCCVER:Mgcc8*}
_cnowarnflags	+=      -Wno-cast-function-type
.endif
.if defined(NO_WARRAY_BOUNDS)
_cnowarnflags	+=      -Wno-array-bounds
.endif
.if defined(NO_STRICT_OVERFLOW)
CFLAGS		+=	-fno-strict-overflow
.endif
.if defined(NO_STRICT_ALIASING)
CFLAGS		+=      -fno-strict-aliasing
.endif


# Add -Wno-foo flags last
.if !defined(WARNS_AUDIT)
CWARNFLAGS	+=	${_cnowarnflags}
.endif

# Allow user-specified additional warning flags
CFLAGS		+=	${CWARNFLAGS}

# Tell bmake not to mistake standard targets for things to be searched for
# or expect to ever be up-to-date
PHONY_NOTMAIN = afterdepend afterinstall all beforedepend beforeinstall \
	beforelinking build build-tools buildfiles buildincludes \
	checkdpadd clean cleandepend cleandir cleanobj configure \
	depend dependall distclean distribute exe extract fetch \
	html includes install installfiles installincludes lint \
	obj objlink objs objwarn patch realall realdepend \
	realinstall regress subdir-all subdir-depend subdir-install \
	tags whereobj

# if given PROG matches anything in the PHONY list, exclude it.
.PHONY: ${PHONY_NOTMAIN:N${PROG:U}}
.NOTMAIN: ${PHONY_NOTMAIN}
