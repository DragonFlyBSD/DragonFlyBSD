PROG=	udevd
SRCS=	udevd.c udevd_client.c udevd_monitor.c udevd_pdev.c 
SRCS+=	udevd_socket.c
LDADD=	-lprop -lpthread
DPADD=	${LIBPROP} ${LIBPTHREAD}
MAN=	udevd.8

NOSHARED=yes

.include <bsd.prog.mk>
