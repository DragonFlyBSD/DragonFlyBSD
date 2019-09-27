# libdmsg - hammer2 network cluster messaging library
#
LIB=	dmsg
SRCS+=	debug.c subs.c crypto.c msg.c msg_lnk.c service.c

CFLAGS+=	-pthread
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include

INCS=	dmsg.h

.PATH: ${.CURDIR}/../../sys/libkern
SRCS+= icrc32.c

.include <bsd.lib.mk>
