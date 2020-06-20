#	@(#)Makefile	8.1 (Berkeley) 6/6/93

PROG=	tail
MAN=	tail.1 tac.1
SRCS=	forward.c misc.c read.c reverse.c tail.c

LINKS=	${BINDIR}/tail ${BINDIR}/tac

.if defined(BOOTSTRAPPING)
CFLAGS+= -DBOOTSTRAPPING
.endif

.include <bsd.prog.mk>
