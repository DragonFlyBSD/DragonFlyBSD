# $FreeBSD: src/secure/libexec/Makefile,v 1.4.2.6 2002/07/05 11:10:51 des Exp $
# $DragonFly: src/secure/libexec/Makefile,v 1.4 2005/09/06 18:55:31 dillon Exp $

.if !defined(NO_OPENSSL) && !defined(NO_OPENSSH)
SUBDIR+=sftp-server ssh-keysign ssh-pkcs11-helper
.endif

.include <bsd.subdir.mk>
