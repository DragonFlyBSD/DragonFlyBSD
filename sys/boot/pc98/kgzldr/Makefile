# $FreeBSD: src/sys/boot/pc98/kgzldr/Makefile,v 1.10 2003/06/30 00:20:28 ru Exp $
# $DragonFly: src/sys/boot/pc98/kgzldr/Attic/Makefile,v 1.3 2003/11/10 06:08:39 dillon Exp $

SRCS=	start.s boot.c inflate.c lib.c crt.s sio.s
OBJS=	${SRCS:N*.h:R:S/$/.o/g}
CFLAGS=	-ffreestanding
CFLAGS+=-Os
CFLAGS+=-DKZIP
LDFLAGS=-nostdlib -static -Wl,-r
.PATH:	${.CURDIR}/../../../kern

.PATH:	${.CURDIR}/../../i386/kgzldr
AFLAGS+=--defsym PC98=1

CLEANFILES=kgzldr.o

BOOT_COMCONSOLE_PORT?=	0x238
AFLAGS+=--defsym SIO_PRT=${BOOT_COMCONSOLE_PORT}

all: kgzldr.o

kgzldr.o: ${OBJS}
	${CC} ${LDFLAGS} -o ${.TARGET} ${OBJS}

realinstall:
	${INSTALL} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    kgzldr.o ${DESTDIR}${LIBDIR}

.include <bsd.prog.mk>
