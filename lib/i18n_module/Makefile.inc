# $DragonFly: src/lib/i18n_module/Makefile.inc,v 1.1 2005/03/11 20:47:05 joerg Exp $

NOPROFILE=	1
.include "../Makefile.shlib"

SHLIB_MAJOR=	${MODULE_SHLIB_MAJOR}

BASENAME=	${.CURDIR:T}
LIB=		${BASENAME}
SRCS?=		${SRCPRE:L}${BASENAME:L}.c
LIBDIR=		/usr/lib/i18n

CITRUSDIR=	${.CURDIR}/../../libc/citrus

CFLAGS+=	-I${CITRUSDIR}
CFLAGS+=	-DLOCALEMOD_MAJOR=${MODULE_SHLIB_MAJOR}

.PATH: ${CITRUSDIR}/modules
