# $FreeBSD: src/secure/lib/Makefile,v 1.19.2.5 2003/04/04 21:33:15 ru Exp $

SUBDIR= libcipher
.if !defined(NO_OPENSSL)
SUBDIR+=libcrypto libssl
SUBDIR+=libssh
.endif

# maximum parallelism
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
