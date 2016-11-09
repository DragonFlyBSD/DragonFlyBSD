PROG=		cryptdisks
MAN=		cryptdisks.8
NOSHARED=	YES

SRCS+=		cryptdisks.c safe_mem.c

LDADD=		-lcryptsetup -lluks -ldm -ltcplay -lprop -lutil
DPADD=		${LIBCRYPTSETUP} ${LIBLUKS} ${LIBDM} ${LIBTCPLAY}
DPADD+=		${LIBPROP} ${LIBUTIL}

DPADD+=		${LIBRECRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include

.include <bsd.prog.mk>
