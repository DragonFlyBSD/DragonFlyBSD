# $FreeBSD: src/secure/usr.bin/Makefile,v 1.14.2.4 2002/07/05 11:11:01 des Exp $
# $DragonFly: src/secure/usr.bin/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $

SUBDIR= bdes
.if !defined(NOCRYPT) && !defined(NO_OPENSSL) && !defined(MAKE_KERBEROS4) && !defined(MAKE_KERBEROS5)
SUBDIR+=telnet
.endif
.if !defined(NO_OPENSSL)
SUBDIR+=openssl
.if !defined(NO_OPENSSH)
SUBDIR+=scp sftp ssh ssh-add ssh-agent ssh-keygen ssh-keyscan
.endif
.endif

.include <bsd.subdir.mk>
