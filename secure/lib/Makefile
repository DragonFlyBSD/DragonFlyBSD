# $FreeBSD: src/secure/lib/Makefile,v 1.19.2.5 2003/04/04 21:33:15 ru Exp $
# $DragonFly: src/secure/lib/Makefile,v 1.2 2003/06/17 04:27:48 dillon Exp $

SUBDIR= libcipher
.if !defined(NOCRYPT) && !defined(NO_OPENSSL) && !defined(MAKE_KERBEROS4) && !defined(MAKE_KERBEROS5)
SUBDIR+=libtelnet
.endif
.if !defined(NO_OPENSSL)
SUBDIR+=libcrypto libssl
.if !defined(NO_OPENSSH)
SUBDIR+=libssh
.endif
.endif

.include <bsd.subdir.mk>
