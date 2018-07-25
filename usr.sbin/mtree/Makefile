#	$NetBSD: Makefile,v 1.34 2013/02/03 19:15:16 christos Exp $
#	from: @(#)Makefile	8.2 (Berkeley) 4/27/95

.PATH: ${.CURDIR}/../../usr.bin/cksum

PROG=	mtree
MAN=	mtree.8
SRCS=	compare.c crc.c create.c excludes.c misc.c mtree.c spec.c verify.c
SRCS+=	specspec.c getid.c only.c

SRCS+=	pack_dev.c stat_flags.c

DPADD+=	${LIBUTIL}
LDADD+=	-lutil

.if !defined(BOOTSTRAPPING)
.PATH: ${.CURDIR}/../../sbin/md5
SRCS+=	sha1hl.c
DPADD+=	${LIBMD} ${LIBCRYPTO}
LDADD+=	-lmd -lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
CFLAGS+= 	-I${.CURDIR}/../../crypto/libressl/include
CFLAGS+=	-I${.CURDIR}/../../sbin/md5
.else
CFLAGS+=-DNO_MD5 -DNO_SHA1 -DNO_SHA2 -DNO_RMD160
.if ${WORLD_VERSION} < 500302
.PATH: ${.CURDIR}/../../lib/libc/gen
SRCS+=	pwcache.c vis.c
CFLAGS+=-I${.CURDIR}/../../lib/libc/include
CFLAGS+=-include ${.CURDIR}/../../include/grp.h
CFLAGS+=-include ${.CURDIR}/../../include/pwd.h
CFLAGS+=-include ${.CURDIR}/../../include/vis.h
.endif
.endif

#CFLAGS+=-DDEBUG

.include <bsd.prog.mk>
