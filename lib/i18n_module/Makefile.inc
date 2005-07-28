# $DragonFly: src/lib/i18n_module/Makefile.inc,v 1.2 2005/07/28 19:43:55 joerg Exp $

NOPROFILE=	1
.include "../Makefile.shlib"

SHLIB_MAJOR=	${MODULE_SHLIB_MAJOR}

BASENAME=	${.CURDIR:T}
LIB=		${BASENAME}
SRCS?=		${SRCPRE:L}${BASENAME:L}.c

TARGET_LIBDIR=		/usr/lib/i18n
TARGET_SHLIBDIR=	/usr/lib/i18n

CITRUSDIR=	${.CURDIR}/../../libc/citrus

CFLAGS+=	-I${CITRUSDIR}
CFLAGS+=	-DLOCALEMOD_MAJOR=${MODULE_SHLIB_MAJOR}

.PATH: ${CITRUSDIR}/modules
