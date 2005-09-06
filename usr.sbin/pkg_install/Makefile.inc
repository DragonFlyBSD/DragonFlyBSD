# $FreeBSD: src/usr.sbin/pkg_install/Makefile.inc,v 1.10 2004/01/17 13:41:16 ru Exp $
# $DragonFly: src/usr.sbin/pkg_install/Attic/Makefile.inc,v 1.5 2005/09/06 18:55:46 dillon Exp $

.if exists(${.OBJDIR}/../lib)
LIBINSTALL=	${.OBJDIR}/../lib/libinstall.a
.else
LIBINSTALL=	${.CURDIR}/../lib/libinstall.a
.endif

.if !defined(NO_CRYPT) && !defined(NO_OPENSSL) && \
    defined(LDADD) && ${LDADD:M-lfetch} != ""
DPADD+=		${LIBSSL} ${LIBCRYPTO}
LDADD+=		-lssl -lcrypto
.endif

# Inherit BINDIR from one level up.
.if exists(${.CURDIR}/../../Makefile.inc)
.include "${.CURDIR}/../../Makefile.inc"
.endif
