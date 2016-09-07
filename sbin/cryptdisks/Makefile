PROG=		cryptdisks
MAN=		cryptdisks.8
NOSHARED=	YES

SRCS+=		cryptdisks.c safe_mem.c

LDADD=		-lcryptsetup -lluks -ldm -ltcplay -lprop -lutil
DPADD=		${LIBCRYPTSETUP} ${LIBLUKS} ${LIBDM} ${LIBTCPLAY}
DPADD+=		${LIBPROP} ${LIBUTIL}

DPADD+=		${LIBRECRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	-rpath /lib/priv -L ${_SHLIBDIRPREFIX}/usr/lib/priv
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include

.include <bsd.prog.mk>
