PROG=		cryptdisks
MAN=		cryptdisks.8

SRCS+=		cryptdisks.c safe_mem.c

LDADD+=		-lcryptsetup -lluks -ldm -ltcplay -lprop -lcrypto -lutil
DPADD+=		${LIBCRYPTSETUP} ${LIBLUKS} ${LIBDM} ${LIBTCPLAY}
DPADD+=		${LIBPROP} ${LIBCRYPTO} ${LIBUTIL}

.include <bsd.prog.mk>
