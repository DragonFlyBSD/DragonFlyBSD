# $FreeBSD: src/secure/usr.bin/Makefile,v 1.14.2.4 2002/07/05 11:11:01 des Exp $
# $DragonFly: src/secure/usr.bin/Makefile,v 1.3 2003/08/05 07:45:43 asmodai Exp $

SUBDIR= bdes
.if !defined(NOCRYPT) && !defined(NO_OPENSSL) && !defined(MAKE_KERBEROS5)
SUBDIR+=telnet
.endif
.if !defined(NO_OPENSSL)
SUBDIR+=openssl
.if !defined(NO_OPENSSH)
SUBDIR+=scp sftp ssh ssh-add ssh-agent ssh-keygen ssh-keyscan
.endif
.endif

.include <bsd.subdir.mk>
