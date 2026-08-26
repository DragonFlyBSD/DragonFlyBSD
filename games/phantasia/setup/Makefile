.PATH: ${.CURDIR}/..

PROG=	setup
SRCS=	phantglobs.c setup.c

# for floor()
LDADD+=	-lm
DPADD+=	${LIBM}

CFLAGS+=-DCOMPILING_SETUP -I${.CURDIR}/..

.include <bsd.hostprog.mk>
