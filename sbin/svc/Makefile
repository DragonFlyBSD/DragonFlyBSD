# service manager
#

PROG=	svc
MAN=	svc.8
NOSHARED?= yes	# dep on libpthread.so.0 in /sbin

SRCS=	svc.c subs.c remote.c execute.c
CFLAGS+=-pthread
DPADD=	${LIBPTHREAD}

.include <bsd.prog.mk>
