AWKSRC= ${.CURDIR}/../../../contrib/awk
.PATH: ${AWKSRC}

PROG=	maketab
SRCS=	maketab.c awkgram.tab.h

CFLAGS+= -I. -I${AWKSRC} -DHAS_ISBLANK

y.tab.h: awkgram.c awkgram.y

awkgram.tab.h: y.tab.h
	${LN} -sf ${.ALLSRC} ${.TARGET}

WARNS= 2

CLEANFILES+=	awkgram.tab.h y.tab.h awkgram.c

build-tools: maketab.nx

# this setup is used in place, it is not installed anywhere
install:

.include <bsd.hostprog.mk>
