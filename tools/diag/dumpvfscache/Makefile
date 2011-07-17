
PROG=	dumpvfscache

LDADD=	-lkvm -lmd
DPADD=	${LIBKVM} ${LIBMD}

NOMAN=	1

test:	${PROG}
	./${PROG} > a

.include <bsd.prog.mk>
