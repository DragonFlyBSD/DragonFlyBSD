TARGETS=/usr/local/bin/eatmem /usr/local/bin/eattls \
	/usr/local/bin/t_mlock /usr/local/bin/t_mlockall

CFLAGS= -O -g

all:	${TARGETS}

/usr/local/bin/eatmem: eatmem.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/usr/local/bin/eattls: eattls.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/usr/local/bin/t_mlock: t_mlock.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/usr/local/bin/t_mlockall: t_mlockall.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

clean:
	rm -f ${TARGETS}
