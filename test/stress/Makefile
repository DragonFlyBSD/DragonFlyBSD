# $DragonFly: src/test/stress/Makefile,v 1.1 2003/08/25 19:41:00 dillon Exp $
#

TARGETS=/usr/local/bin/eatmem

CFLAGS= -O -g -I/usr/src/sys

all:	$(TARGETS)

/usr/local/bin/eatmem: eatmem.c
	$(CC) $(CFLAGS) eatmem.c -o ${.TARGET}

clean:
	rm -f $(TARGETS)
