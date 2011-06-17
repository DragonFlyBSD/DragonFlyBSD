PROG=		cryptdisks
MAN=		cryptdisks.8

SRCS+=		cryptdisks.c safe_mem.c

LDADD+=		-L${.CURDIR}/../../gnu/lib/libcryptsetup -lcryptsetup
DPADD+=		${.CURDIR}/../../gnu/lib/libcryptsetup/libcryptsetup.a

LDADD+=		-L${.CURDIR}/../../gnu/lib/libluks -lluks
DPADD+=		${.CURDIR}/../../gnu/lib/libluks/libluks.a

LDADD+=		-L${.CURDIR}/../../gnu/lib/libdevmapper -ldevmapper
DPADD+=		${.CURDIR}/../../gnu/lib/libdevmapper/libdevmapper.a

LDADD+=		-lprop -lcrypto

.include <bsd.prog.mk>
