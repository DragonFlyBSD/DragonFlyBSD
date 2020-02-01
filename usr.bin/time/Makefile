#	@(#)Makefile	8.1 (Berkeley) 6/6/93

PROG=	time

.if defined(BOOTSTRAPPING)
CFLAGS+= -DBOOTSTRAPPING
.else
DPADD=	${LIBKINFO}
LDADD=	-lkinfo
.endif

.include <bsd.prog.mk>
