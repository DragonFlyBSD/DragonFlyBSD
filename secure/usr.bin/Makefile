# $FreeBSD: src/secure/usr.bin/Makefile,v 1.14.2.4 2002/07/05 11:11:01 des Exp $
# $DragonFly: src/secure/usr.bin/Makefile,v 1.5 2008/05/16 11:44:56 corecode Exp $

SUBDIR= bdes
.if !defined(NO_OPENSSL)
SUBDIR+=openssl
.if !defined(NO_OPENSSH)
SUBDIR+=scp sftp ssh ssh-add ssh-agent ssh-keygen ssh-keyscan ssh-vulnkey
.endif
.endif

.include <bsd.subdir.mk>
