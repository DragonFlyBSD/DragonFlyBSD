# $FreeBSD: src/secure/lib/Makefile,v 1.19.2.5 2003/04/04 21:33:15 ru Exp $

SUBDIR= libcipher
.if defined(FORCE_OPENSSL) # to be removed during 4.8 development
SUBDIR+=libcrypto libssl
.endif
.if !defined(NO_LIBRESSL)
SUBDIR+=librecrypto libressl libssh
.endif

# maximum parallelism
# Note: libssl depends on libcrypto and libressl depends on librecrypto
#       This dependency is taken care of by Makefile.inc1
#
SUBDIR_ORDERED=

.include <bsd.subdir.mk>
