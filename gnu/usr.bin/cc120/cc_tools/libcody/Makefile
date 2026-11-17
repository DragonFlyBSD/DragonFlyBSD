GCC_NO_PATH=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH: ${GCCDIR}/libcody

LIB=		cody

CFLAGS+=	-DSRCDIR="\"${GCCDIR}\""

# From libcody/Makefile.in
LIBCODY.O=	\
	buffer.o client.o fatal.o netclient.o netserver.o \
	resolver.o packet.o server.o

SRCS=	${LIBCODY.O:.o=.cc}

CLEANFILES+=	config.h
config.h: ${.CURDIR}/../../support-libs/libcody/config.h
	cp ${.ALLSRC} ${.TARGET}

beforedepend: config.h

.include <bsd.hostlib.mk>
