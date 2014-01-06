# $FreeBSD: src/share/mk/bsd.sys.mk,v 1.3.2.5 2002/07/03 16:59:14 des Exp $
#
# This file contains common settings used for building DragonFly
# sources.

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

# Enable various levels of compiler warning checks.  These may be
# overridden (e.g. if using a non-gcc compiler) by defining NO_WARNS.

.if !defined(NO_WARNS)
. if defined(WARNS)
.  if ${WARNS} >= 1
CWARNFLAGS	+=	-Wmissing-include-dirs -Wsystem-headers
.   if !defined(NO_WERROR) && (${CCVER} == "gcc44" || ${CCVER} == "gcc47")
CWARNFLAGS	+=	-Werror
.   endif
.  endif
.  if ${WARNS} >= 2
CWARNFLAGS	+=	-Wall -Wformat-security -Wno-pointer-sign
.  endif
.  if ${WARNS} >= 3
CWARNFLAGS	+=	-Wextra -Wno-unused-parameter -Wstrict-prototypes\
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
.  if ${WARNS} >= 2 && ${WARNS} <= 4
# XXX Delete -Wuninitialized by default for now -- the compiler doesn't
# XXX always get it right.
CWARNFLAGS	+=	-Wno-uninitialized
.  endif
# Activate gcc47's -Wunused-but-set-variable (which is in -Wall) and
# -Wunused-but-set-parameter (which is in -Wextra) only at WARNS >= 4
# (which is the level when also -Wunused-parameter comes into play).
.  if ${WARNS} >= 2 && ${WARNS} <= 3 && ${CCVER} == "gcc47"
CWARNFLAGS	+=	-Wno-unused-but-set-variable
.  endif
.  if ${WARNS} == 3 && ${CCVER} == "gcc47"
CWARNFLAGS	+=	-Wno-unused-but-set-parameter
.  endif
. endif

. if defined(FORMAT_AUDIT)
WFORMAT		=	1
. endif
. if defined(WFORMAT)
.  if ${WFORMAT} > 0
CWARNFLAGS	+=	-Wformat=2 -Wno-format-extra-args
.   if !defined(NO_WERROR) && (${CCVER} == "gcc47" || ${CCVER} == "gcc44")
CWARNFLAGS	+=	-Werror
.   endif
.  endif
. endif
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

.PHONY: ${PHONY_NOTMAIN}
.NOTMAIN: ${PHONY_NOTMAIN}
