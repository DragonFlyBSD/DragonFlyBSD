CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
.include "../Makefile.inc"

.PATH: ${GCCDIR}/libcody

CFLAGS+=	-include config.h.in -DSRCDIR="\"${GCCDIR}\""

LIB=		cody
INTERNALLIB=	YES
SHLIB_MAJOR=	0

# From libcody Makefile (LIBCODY.O)
SRCS= \
	buffer.cc client.cc fatal.cc netclient.cc netserver.cc \
	resolver.cc packet.cc server.cc

INCS+=		cody.hh
INCSDIR=	/usr/libdata/gcc${GCCSHORTVER}

.include <bsd.lib.mk>
