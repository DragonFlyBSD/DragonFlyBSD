TARGETS=/tmp/lockf

all:	${TARGETS}

/tmp/lockf: lockf.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

clean:
	rm -f ${TARGETS}
