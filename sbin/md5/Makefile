#	@(#)Makefile	8.1 (Berkeley) 6/9/93
# $FreeBSD: src/sbin/md5/Makefile,v 1.10 2005/03/09 19:23:04 cperciva Exp $
# $DragonFly: src/sbin/md5/Makefile,v 1.7 2008/11/03 00:25:45 pavalos Exp $

PROG=	md5

SRCS=	md5.c sha1hl.c

LINKS=	${BINDIR}/md5 ${BINDIR}/rmd160 \
	${BINDIR}/md5 ${BINDIR}/sha1 \
	${BINDIR}/md5 ${BINDIR}/sha256

MLINKS=	md5.1 rmd160.1 \
	md5.1 sha1.1 \
	md5.1 sha256.1

DPADD=	${LIBMD} ${LIBCRYPTO}
LDADD=	-lmd -lprivate_crypto
LDFLAGS+= -rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv

.include <bsd.prog.mk>
