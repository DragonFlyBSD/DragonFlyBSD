# $FreeBSD: src/secure/usr.sbin/Makefile,v 1.1.2.1 2000/07/30 22:26:29 kris Exp $
# $DragonFly: src/secure/usr.sbin/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $

SUBDIR= 
.if !defined(NO_OPENSSH) && !defined(NO_OPENSSL)
SUBDIR+=sshd
.endif

.include <bsd.subdir.mk>
