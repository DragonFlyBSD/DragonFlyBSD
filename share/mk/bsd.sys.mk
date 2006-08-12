# $FreeBSD: src/share/mk/bsd.sys.mk,v 1.3.2.5 2002/07/03 16:59:14 des Exp $
# $DragonFly: src/share/mk/bsd.sys.mk,v 1.9 2006/08/12 22:34:23 swildner Exp $
#
# This file contains common settings used for building DragonFly
# sources.

# Enable various levels of compiler warning checks.  These may be
# overridden (e.g. if using a non-gcc compiler) by defining NO_WARNS.

.if !defined(NO_WARNS)
. if defined(WARNS)
# XXX Delete -Wuninitialized by default for now -- the compiler doesn't
# XXX always get it right.
.  if ${WARNS} <= 4
CFLAGS		+=	-Wno-uninitialized
.  endif
.  if defined(WARNS_WERROR) && !defined(NO_WERROR)
CFLAGS		+=	-Werror
.  endif
.  if ${WARNS} > 0
CFLAGS		+=	-Wunknown-pragmas -Wsystem-headers
.endif
.  if ${WARNS} > 1
CFLAGS		+=	-Wall
.  endif
.  if ${WARNS} > 2
CFLAGS		+=	-W -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
.  endif
.  if ${WARNS} > 3
CFLAGS		+=	-Wreturn-type -Wcast-qual -Wwrite-strings -Wswitch -Wshadow -Wcast-align
.  endif
.  if ${WARNS} > 5
CFLAGS		+=	-Wchar-subscripts -Winline -Wnested-externs -Wredundant-decls
.  endif
. endif

. if defined(FORMAT_AUDIT)
WFORMAT		=	1
. endif
. if defined(WFORMAT)
.  if ${WFORMAT} > 0
CFLAGS		+=	-Wno-format-extra-args
.   if defined(WARNS_WERROR) && !defined(NO_WERROR)
CFLAGS		+=	-Werror
.   endif
.  endif
. endif
.endif

.if defined(WARNS_NO_UNUSED_PARAMETERS)
CFLAGS+=	-Wno-unused-parameters
.endif

# Allow user-specified additional warning flags
CFLAGS		+=	${CWARNFLAGS}
