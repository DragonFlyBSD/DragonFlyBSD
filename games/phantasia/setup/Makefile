.PATH: ${.CURDIR}/..

PROG=	setup
SRCS=	phantglobs.c setup.c

# for floor()
LDADD+=	-lm
DPADD+=	${LIBM}

CFLAGS+=-DCOMPILING_SETUP -I${.CURDIR}/..

build-tools: setup.nx

# this setup is used in place, it is not installed anywhere
install:

.include <bsd.hostprog.mk>
