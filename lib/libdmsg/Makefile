# libdmsg - hammer2 network cluster messaging library
#
LIB=	dmsg
SRCS+=	icrc.c debug.c subs.c crypto.c msg.c msg_lnk.c service.c

CFLAGS+=	-pthread
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include

INCS=	dmsg.h

.include <bsd.lib.mk>
