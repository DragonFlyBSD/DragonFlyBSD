# $DragonFly: src/usr.sbin/vknetd/Makefile,v 1.1 2008/05/27 01:58:01 dillon Exp $
#

PROG=	vknetd
MAN=	vknetd.8
CFLAGS += -DUSE_PTHREADS=1 -pthread
SRCS=	vknetd.c bridge.c mac.c filter.c

.include <bsd.prog.mk>
