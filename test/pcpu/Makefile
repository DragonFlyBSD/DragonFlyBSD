# $DragonFly: src/test/pcpu/Makefile,v 1.2 2005/08/08 03:31:00 hmp Exp $
#

DESTDIR=	/usr/local/bin
TARGETS=	$(DESTDIR)/ncache-stats $(DESTDIR)/cpustat

CFLAGS= 	-O -g -I/usr/src/sys

all:	$(TARGETS)

$(DESTDIR)/ncache-stats: ncache-stats.c
	$(CC) $(CFLAGS) -lkvm ncache-stats.c -o ${.TARGET}

$(DESTDIR)/cpustat:	cpustat.c
	$(CC) $(CFLAGS) -lkinfo cpustat.c -o ${.TARGET}

clean:
	rm -f $(TARGETS)
