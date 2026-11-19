.include <bsd.dragonfly.mk>

LIBRESSL_SRC=	${DF.SRCTOP}/crypto/libressl
LCRYPTO_SRC=	${LIBRESSL_SRC}/crypto
LSSL_SRC=	${LIBRESSL_SRC}/ssl

CFLAGS+=	-I${LCRYPTO_SRC} -I${LSSL_SRC} -I${.OBJDIR}
CFLAGS+=	-I${LIBRESSL_SRC}/include
