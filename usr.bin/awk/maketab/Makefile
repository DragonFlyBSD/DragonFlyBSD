AWKSRC= ${.CURDIR}/../../../contrib/awk
.PATH: ${AWKSRC}

PROG=	maketab
SRCS=	maketab.c ytab.h

CFLAGS+= -I. -I${AWKSRC} -DHAS_ISBLANK

y.tab.h: awkgram.c awkgram.y

ytab.h: y.tab.h
	${LN} -sf ${.ALLSRC} ${.TARGET}

WARNS= 2

CLEANFILES+=	ytab.h y.tab.h awkgram.c

build-tools: maketab.nx

# this setup is used in place, it is not installed anywhere
install:

.include <bsd.hostprog.mk>
