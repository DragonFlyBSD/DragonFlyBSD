# $FreeBSD: src/gnu/usr.bin/cc/protoize/Makefile,v 1.2 2002/05/10 08:54:49 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/protoize/Attic/Makefile,v 1.1 2004/01/17 22:40:16 drhodus Exp $

.include "../Makefile.inc"

.PATH: ${.CURDIR}/../cc_tools ${GCCDIR}

PROG=	protoize3
SRCS=	protoize.c
NOMAN=

# things are rather hard-coded, we work around that here
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"../libdata/gcc\"

DPADD=	${LIBCC_INT}
LDADD=	${LIBCC_INT}

CLEANFILES=	config.h
config.h: auto-host.h freebsd-native.h
	cat ${.ALLSRC} >${.TARGET}

.include <bsd.prog.mk>
