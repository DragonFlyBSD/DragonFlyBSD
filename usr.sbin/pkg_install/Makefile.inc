# $FreeBSD: src/usr.sbin/pkg_install/Makefile.inc,v 1.10 2004/01/17 13:41:16 ru Exp $
# $DragonFly: src/usr.sbin/pkg_install/Attic/Makefile.inc,v 1.3 2004/07/30 04:46:11 dillon Exp $

.if exists(${.OBJDIR}/../lib)
LIBINSTALL=	${.OBJDIR}/../lib/libinstall.a
.else
LIBINSTALL=	${.CURDIR}/../lib/libinstall.a
.endif

.if !defined(NOCRYPT) && !defined(NO_OPENSSL) && \
    defined(LDADD) && ${LDADD:M-lfetch} != ""
DISTRIBUTION=	crypto
DPADD+=		${LIBSSL} ${LIBCRYPTO}
LDADD+=		-lssl -lcrypto
.endif

# turn off for now (fails w/ gcc34)
WFORMAT=0

# Inherit BINDIR from one level up.
.if exists(${.CURDIR}/../../Makefile.inc)
.include "${.CURDIR}/../../Makefile.inc"
.endif
