# $FreeBSD: src/sys/boot/i386/btx/lib/Makefile,v 1.3.2.2 2002/11/15 19:46:41 ru Exp $
# $DragonFly: src/sys/boot/i386/btx/lib/Attic/Makefile,v 1.2 2003/06/17 04:28:18 dillon Exp $

OBJS=		btxcsu.o btxsys.o btxv86.o
CLEANFILES+=	crt0.o ${OBJS}

all: crt0.o

crt0.o: ${OBJS}
	${LD} ${LDFLAGS} -i -o ${.TARGET} ${OBJS}

.include <bsd.lib.mk>

.s.o:
	${AS} ${AFLAGS} -o ${.TARGET} ${.IMPSRC}
