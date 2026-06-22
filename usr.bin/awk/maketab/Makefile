AWKSRC= ${.CURDIR}/../../../contrib/awk
.PATH: ${AWKSRC}

PROG=	maketab
SRCS=	maketab.c awkgram.tab.h

CFLAGS+= -I. -I${AWKSRC} -DHAS_ISBLANK

y.tab.h: awkgram.c awkgram.y

awkgram.tab.h: y.tab.h
	${LN} -sf ${.ALLSRC} ${.TARGET}

# remaining warnings:
# - redundant-decls: popen()/pclose() in proto.h
WARNS= 5

CLEANFILES+=	awkgram.tab.h y.tab.h awkgram.c

.include <bsd.hostprog.mk>
