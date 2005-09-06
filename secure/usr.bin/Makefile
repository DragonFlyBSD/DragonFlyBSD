# $FreeBSD: src/secure/usr.bin/Makefile,v 1.14.2.4 2002/07/05 11:11:01 des Exp $
# $DragonFly: src/secure/usr.bin/Makefile,v 1.4 2005/09/06 18:55:33 dillon Exp $

SUBDIR= bdes
.if !defined(NO_CRYPT) && !defined(NO_OPENSSL) && !defined(WANT_KERBEROS)
SUBDIR+=telnet
.endif
.if !defined(NO_OPENSSL)
SUBDIR+=openssl
.if !defined(NO_OPENSSH)
SUBDIR+=scp sftp ssh ssh-add ssh-agent ssh-keygen ssh-keyscan
.endif
.endif

.include <bsd.subdir.mk>
