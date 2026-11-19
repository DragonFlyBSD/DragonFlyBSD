.include <bsd.dragonfly.mk>

SRCDIR=	${DF.SRCTOP}/contrib/gmp
.PATH: ${SRCDIR}

WARNS?=	1

.include "../Makefile.inc"
