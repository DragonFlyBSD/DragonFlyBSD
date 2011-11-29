# $FreeBSD: src/secure/lib/Makefile,v 1.19.2.5 2003/04/04 21:33:15 ru Exp $
# $DragonFly: src/secure/lib/Makefile,v 1.4 2005/09/06 18:55:27 dillon Exp $

SUBDIR= libcipher
.if !defined(NO_OPENSSL)
SUBDIR+=libcrypto libssl
.if !defined(NO_OPENSSH)
SUBDIR+=libssh
.endif
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
