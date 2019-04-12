.if exists (${.CURDIR}/../../Makefile.inc1)
LIBRESSL_SRC=	${.CURDIR}/../../crypto/libressl
.elif exists (${.CURDIR}/../../../Makefile.inc1)
LIBRESSL_SRC=	${.CURDIR}/../../../crypto/libressl
.else
.error "Cannot find base directory"
.endif
LCRYPTO_SRC=	${LIBRESSL_SRC}/crypto
LSSL_SRC=	${LIBRESSL_SRC}/ssl

CFLAGS+=	-I${LCRYPTO_SRC} -I${LSSL_SRC} -I${.OBJDIR}
CFLAGS+=	-I${LIBRESSL_SRC}/include
