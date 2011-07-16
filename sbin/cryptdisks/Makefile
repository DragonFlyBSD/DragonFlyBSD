PROG=		cryptdisks
MAN=		cryptdisks.8

SRCS+=		cryptdisks.c safe_mem.c

LDADD+=		-L${.CURDIR}/../../gnu/lib/libcryptsetup -lcryptsetup
DPADD+=		${.CURDIR}/../../gnu/lib/libcryptsetup/libcryptsetup.a

LDADD+=		-L${.CURDIR}/../../gnu/lib/libluks -lluks
DPADD+=		${.CURDIR}/../../gnu/lib/libluks/libluks.a

LDADD+=		-L${.CURDIR}/../../gnu/lib/libdm -ldm
DPADD+=		${.CURDIR}/../../gnu/lib/libdm/libdm.a

LDADD+=		-L${.CURDIR}/../../lib/tcplay -ltcplay
DPADD+=		${.CURDIR}/../../lib/tcplay/tcplay.a

LDADD+=		-lprop -lcrypto -lutil

.include <bsd.prog.mk>
