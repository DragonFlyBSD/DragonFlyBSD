# $FreeBSD: src/usr.sbin/pkg_install/Makefile,v 1.15 2004/01/17 13:41:16 ru Exp $
# $DragonFly: src/usr.sbin/pkg_install/Attic/Makefile,v 1.7 2005/09/06 18:55:46 dillon Exp $

SUBDIR=	lib add create delete info ${_sign} version

.if !defined(NO_CRYPT) && !defined(NO_OPENSSL)
_sign=	sign
.endif

.include <bsd.subdir.mk>
