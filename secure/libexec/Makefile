# $FreeBSD: src/secure/libexec/Makefile,v 1.4.2.6 2002/07/05 11:10:51 des Exp $
# $DragonFly: src/secure/libexec/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $

.if !defined(NOCRYPT) && !defined(MAKE_KERBEROS4) && !defined(MAKE_KERBEROS5) && !defined(NO_OPENSSL)
SUBDIR= telnetd
.endif
.if !defined(NO_OPENSSL) && !defined(NO_OPENSSH)
SUBDIR+=sftp-server ssh-keysign
.endif

.include <bsd.subdir.mk>
