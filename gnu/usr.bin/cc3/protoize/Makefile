# $FreeBSD: src/gnu/usr.bin/cc/protoize/Makefile,v 1.2 2002/05/10 08:54:49 obrien Exp $
# $DragonFly: src/gnu/usr.bin/cc3/protoize/Attic/Makefile,v 1.3 2004/02/02 23:35:12 dillon Exp $

.include "../Makefile.inc"

CFLAGS+=	-I${.CURDIR}/../config

.PATH: ${.CURDIR}/../cc_tools ${GCCDIR}/gcc

PROG=	protoize
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
