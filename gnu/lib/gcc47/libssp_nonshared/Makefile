.include "../Makefile.inc"
.PATH: ${GCCDIR}/libssp

LIB=		ssp_nonshared

SRCS+=		ssp-local.c

CFLAGS+=	-fPIC
CFLAGS+=	-DPIC

.include <bsd.lib.mk>
