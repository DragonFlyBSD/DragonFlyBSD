# $FreeBSD: src/usr.sbin/pkg_install/Makefile,v 1.15 2004/01/17 13:41:16 ru Exp $
# $DragonFly: src/usr.sbin/pkg_install/Attic/Makefile,v 1.4 2004/07/30 04:46:11 dillon Exp $

SUBDIR=	lib add create delete info ${_sign} version

.if !defined(NOCRYPT) && !defined(NO_OPENSSL)
_sign=	sign
.endif

.include <bsd.subdir.mk>
