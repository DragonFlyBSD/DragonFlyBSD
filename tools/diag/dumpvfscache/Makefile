
PROG=	dumpvfscache

LDADD=	-lkvm
DPADD=	${LIBKVM}

NOMAN=	1

test:	${PROG}
	./${PROG} > a

.include <bsd.prog.mk>
