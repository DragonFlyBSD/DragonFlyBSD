# $FreeBSD: src/usr.sbin/pkg_install/Makefile,v 1.15 2004/01/17 13:41:16 ru Exp $
# $DragonFly: src/usr.sbin/pkg_install/Attic/Makefile,v 1.5 2005/06/11 21:09:51 asmodai Exp $

.if !defined(NO_PKGTOOLS)
SUBDIR=	lib add create delete info ${_sign} version
.endif

.if !defined(NOCRYPT) && !defined(NO_OPENSSL)
_sign=	sign
.endif

.include <bsd.subdir.mk>
