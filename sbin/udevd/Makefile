PROG=	udevd
SRCS=	udevd.c udevd_client.c udevd_monitor.c udevd_pdev.c 
SRCS+=	udevd_socket.c
LDADD=	-lprop
DPADD=	${LIBPROP}
MAN=	udevd.8

NOSHARED=yes

.include <bsd.prog.mk>
