SRCDIR=	${.CURDIR}/

LIB=	ipfw3layer4
SRCS=	ipfw3_layer4.c

INCS=	ipfw3_layer4.h


WARNS?=	2

LFLAGS+=
YFLAGS+=
CFLAGS+=-DHAVE_CONFIG_H -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=-D_U_="__attribute__((unused))" -DYYBISON
CFLAGS+=-DHAVE_STRUCT_ETHER_ADDR -DHAVE_NET_PFVAR_H
.if !defined(NOINET6)
CFLAGS+=-DINET6
.endif

LDADD=-lpcap
DPADD=${LIBPCAP}
SHLIB_MAJOR=3

#
# Magic to grab sources out of src/contrib
#
LAYER4_DISTDIR?=${SRCDIR}
CFLAGS+=-I${LAYER4_DISTDIR}
.PATH:	${LAYER4_DISTDIR}

.include <bsd.lib.mk>
