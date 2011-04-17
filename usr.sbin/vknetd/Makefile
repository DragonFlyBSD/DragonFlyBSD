PROG=	vknetd
MAN=	vknetd.8
CFLAGS+=-DUSE_PTHREADS=1
LDADD=	-pthread
SRCS=	vknetd.c bridge.c mac.c filter.c

.include <bsd.prog.mk>
