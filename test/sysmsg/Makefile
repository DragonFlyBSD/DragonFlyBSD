# $DragonFly: src/test/sysmsg/Makefile,v 1.1 2003/08/12 02:29:41 dillon Exp $
#

TARGETS=/tmp/sysmsg1 /tmp/sysmsg2

CFLAGS= -O2 -g -I/usr/src/sys

all:	$(TARGETS)

/tmp/sysmsg1: sysmsg1.c sendsys.h
	$(CC) $(CFLAGS) sysmsg1.c -o /tmp/sysmsg1

/tmp/sysmsg2: sysmsg2.c sendsys.h
	$(CC) $(CFLAGS) sysmsg2.c -o /tmp/sysmsg2

clean:
	rm -f $(TARGETS)
