# $DragonFly: src/test/pcpu/Makefile,v 1.1 2004/04/02 05:46:03 hmp Exp $
#

TARGETS=/usr/local/bin/ncache-stats

CFLAGS= -O -g -lkvm -I/usr/src/sys

all:	$(TARGETS)

/usr/local/bin/ncache-stats: ncache-stats.c
	$(CC) $(CFLAGS) ncache-stats.c -o ${.TARGET}

clean:
	rm -f $(TARGETS)
