# $FreeBSD: src/secure/lib/Makefile,v 1.19.2.5 2003/04/04 21:33:15 ru Exp $
# $DragonFly: src/secure/lib/Makefile,v 1.3 2003/08/05 07:45:42 asmodai Exp $

SUBDIR= libcipher
.if !defined(NOCRYPT) && !defined(NO_OPENSSL) && !defined(MAKE_KERBEROS5)
SUBDIR+=libtelnet
.endif
.if !defined(NO_OPENSSL)
SUBDIR+=libcrypto libssl
.if !defined(NO_OPENSSH)
SUBDIR+=libssh
.endif
.endif

.include <bsd.subdir.mk>
