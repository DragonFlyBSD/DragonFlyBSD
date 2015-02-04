SRCDIR=	${.CURDIR}/

LIB=    _ipfw_basic
SRCS=	ipfw_basic.c 

INCS=	ipfw_basic.h 


WARNS?=	2

LFLAGS+=
YFLAGS+=
CFLAGS+=-DHAVE_CONFIG_H -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=-D_U_="__attribute__((unused))" -DYYBISON
CFLAGS+=-DHAVE_STRUCT_ETHER_ADDR -DHAVE_NET_PFVAR_H
.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

SHLIB_MAJOR=3

#
# Magic to grab sources out of src/contrib
#
LAYER2_DISTDIR?=${SRCDIR}
CFLAGS+=-I${LAYER2_DISTDIR}
.PATH:	${LAYER2_DISTDIR}

.include <bsd.lib.mk>
