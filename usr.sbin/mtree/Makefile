#	From: @(#)Makefile	8.1 (Berkeley) 6/6/93
# $FreeBSD: src/usr.sbin/mtree/Makefile,v 1.15.2.5 2001/09/24 15:44:22 ru Exp $

.PATH: ${.CURDIR}/../../usr.bin/cksum
.PATH: ${.CURDIR}/../../sbin/md5

PROG=	mtree
MAN=	mtree.8
SRCS=	compare.c crc.c create.c excludes.c misc.c mtree.c spec.c verify.c

.if defined(BOOTSTRAPPING)
.PATH: ${.CURDIR}/../../lib/libc/gen
SRCS+=	strtofflags.c
.else
SRCS+=	sha1hl.c
CFLAGS+= -DUSE_MD5 -DUSE_SHA1 -DUSE_RMD160
DPADD=  ${LIBMD} ${LIBRECRYPTO}
LDADD=  -lmd -lprivate_crypto
LDFLAGS+= -rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv
.endif

.include <bsd.prog.mk>
