TARGETS=/usr/local/bin/eatmem

CFLAGS= -O -g

all:	${TARGETS}

/usr/local/bin/eatmem: eatmem.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/usr/local/bin/eattls: eattls.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

clean:
	rm -f ${TARGETS}
