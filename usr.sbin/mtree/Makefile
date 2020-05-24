#	$NetBSD: Makefile,v 1.34 2013/02/03 19:15:16 christos Exp $
#	from: @(#)Makefile	8.2 (Berkeley) 4/27/95

.PATH: ${.CURDIR}/../../usr.bin/cksum

PROG=	mtree
MAN=	mtree.8
SRCS=	compare.c crc.c create.c excludes.c misc.c mtree.c spec.c verify.c
SRCS+=	specspec.c only.c

SRCS+=	pack_dev.c stat_flags.c
CFLAGS+=-DHAVE_NETDB_H

.if !defined(BOOTSTRAPPING)
SRCS+= getid.c
.endif

.if !defined(BOOTSTRAPPING) && !defined(RESCUE)
SRCS+= hash.c
CFLAGS+= 	${PRIVATELIB_CFLAGS}
DPADD+=		${LIBCRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
.else
CFLAGS+=-DNO_MD5 -DNO_SHA -DNO_RMD160
CFLAGS+=-DBOOTSTRAPPING
.if ${WORLD_VERSION} < 500302 # pwcache(3)/vis(3) upgrades came in about here
.PATH: ${.CURDIR}/../../lib/libc/gen
SRCS+=	vis.c
CFLAGS+=-include ${.CURDIR}/../../include/vis.h
.endif
.if ${WORLD_VERSION} < 500901 # fparseln(3) was moved from libutil to libc
DPADD+=	${LIBUTIL}
LDADD+=	-lutil
.endif
.endif

#CFLAGS+=-DDEBUG

.include <bsd.prog.mk>
