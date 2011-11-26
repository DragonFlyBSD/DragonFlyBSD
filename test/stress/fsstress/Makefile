# $DragonFly: src/test/stress/fsstress/Makefile,v 1.1 2004/05/07 17:51:02 dillon Exp $
#
CFLAGS= -g -DNO_XFS -I. -D_LARGEFILE64_SOURCE -Wall -Wstrict-prototypes
FILES=  Makefile README fsstress.c global.h xfscompat.h dotest.sample

all: /tmp/fsstress

fsstress.c: global.h xfscompat.h

/tmp/fsstress: fsstress.c
	$(CC) $(CFLAGS) ${.ALLSRC} -o ${.TARGET}

clean:
	rm -f fsstress.o /tmp/fsstress fsstress.core fsstress-1.00.tgz

tar:
	tar czf fsstress-1.00.tgz $(FILES)
