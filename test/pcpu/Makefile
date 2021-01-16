DESTDIR=	/usr/local/bin
TARGETS=	${DESTDIR}/ncache-stats ${DESTDIR}/cpustat

CFLAGS= 	-O -g -I/usr/src/sys

all:	${TARGETS}

${DESTDIR}/ncache-stats: ncache-stats.c
	${CC} ${CFLAGS} ${.ALLSRC} -lkvm -o ${.TARGET}

${DESTDIR}/cpustat: cpustat.c
	${CC} ${CFLAGS} ${.ALLSRC} -lkinfo -o ${.TARGET}

clean:
	rm -f ${TARGETS}
