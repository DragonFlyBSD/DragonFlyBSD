#	$NetBSD: Makefile,v 1.34 2013/02/03 19:15:16 christos Exp $
#	from: @(#)Makefile	8.2 (Berkeley) 4/27/95

.PATH: ${.CURDIR}/../../sbin/md5
.PATH: ${.CURDIR}/../../usr.bin/cksum

PROG=	mtree
MAN=	mtree.8
SRCS=	compare.c crc.c create.c excludes.c misc.c mtree.c spec.c specspec.c \
	verify.c getid.c only.c

SRCS+=	pack_dev.c stat_flags.c
SRCS+=	sha1hl.c

DPADD=  ${LIBMD} ${LIBCRYPTO} ${LIBUTIL}
LDADD=  -lmd -lprivate_crypto -lutil
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
CFLAGS+= 	-I${.CURDIR}/../../crypto/libressl/include

CFLAGS+=-I${_SHLIBDIRPREFIX}/usr/include/priv -I${.CURDIR}/../../sbin/md5

#CFLAGS+=-DDEBUG

.include <bsd.prog.mk>
