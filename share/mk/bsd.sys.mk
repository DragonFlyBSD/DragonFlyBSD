# $FreeBSD: src/share/mk/bsd.sys.mk,v 1.3.2.5 2002/07/03 16:59:14 des Exp $
# $DragonFly: src/share/mk/bsd.sys.mk,v 1.4 2004/07/22 13:41:25 asmodai Exp $
#
# This file contains common settings used for building FreeBSD
# sources.

# Enable various levels of compiler warning checks.  These may be
# overridden (e.g. if using a non-gcc compiler) by defining NO_WARNS.

.if !defined(NO_WARNS)
. if defined(WARNS)
.  if ${WARNS} > 0
CFLAGS		+=	-W -Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
# XXX Delete -Wuninitialized by default for now -- the compiler doesn't
# XXX always get it right.
CFLAGS		+=	-Wno-uninitialized
.   if defined(WARNS_WERROR) && !defined(NO_WERROR)
CFLAGS		+=	-Werror
.   endif
.  endif
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
CFLAGS		+=	-Wnon-const-format -Wno-format-extra-args
.   if defined(WARNS_WERROR) && !defined(NO_WERROR)
CFLAGS		+=	-Werror
.   endif
.  endif
. endif
.endif

# Allow user-specified additional warning flags
CFLAGS		+=	${CWARNFLAGS}

