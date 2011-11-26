# $DragonFly: src/test/stress/Makefile,v 1.2 2005/05/02 19:33:52 dillon Exp $
#

TARGETS=/usr/local/bin/eatmem

CFLAGS= -O -g

all:	$(TARGETS)

/usr/local/bin/eatmem: eatmem.c
	$(CC) $(CFLAGS) eatmem.c -o ${.TARGET}

/usr/local/bin/eattls: eattls.c
	$(CC) $(CFLAGS) eattls.c -o ${.TARGET}

clean:
	rm -f $(TARGETS)
