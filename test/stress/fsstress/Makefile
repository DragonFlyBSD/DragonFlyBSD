CFLAGS= -g -DNO_XFS -I. -D_LARGEFILE64_SOURCE -Wall -Wstrict-prototypes
FILES=  Makefile README fsstress.c global.h xfscompat.h dotest.sample

all: /tmp/fsstress

fsstress.c: global.h xfscompat.h

/tmp/fsstress: fsstress.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

clean:
	rm -f fsstress.o /tmp/fsstress fsstress.core fsstress-1.00.tgz

tar:
	tar czf fsstress-1.00.tgz ${FILES}
