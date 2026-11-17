GCC_NO_PATH=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"

# We could tailor the source list for the tools, but just reuse the whole
# source list for simplicity.
.include "${.CURDIR}/../../support-libs/libiberty/Makefile.src"

LIB=		iberty

CLEANFILES+=	config.h
config.h: ${.CURDIR}/../../support-libs/libiberty/config.h
	cp ${.ALLSRC} ${.TARGET}

beforedepend: config.h
afterdepend: libiberty.na

.include <bsd.hostlib.mk>
