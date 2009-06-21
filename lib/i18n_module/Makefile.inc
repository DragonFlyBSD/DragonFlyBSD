# $DragonFly: src/lib/i18n_module/Makefile.inc,v 1.3 2008/04/10 10:21:00 hasso Exp $

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
WARNS?=		1

.PATH: ${CITRUSDIR} ${CITRUSDIR}/modules 
