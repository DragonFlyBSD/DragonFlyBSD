# $DragonFly: src/test/sysmsg/Makefile,v 1.2 2004/09/02 11:21:12 eirikn Exp $
#

TARGETS=/tmp/sysmsg1 /tmp/sysmsg2

CFLAGS= -O2 -g -I/usr/src/sys

all:	$(TARGETS)

/tmp/sysmsg1: sysmsg1.c syscall.h
	$(CC) $(CFLAGS) sysmsg1.c -o /tmp/sysmsg1

/tmp/sysmsg2: sysmsg2.c syscall.h
	$(CC) $(CFLAGS) sysmsg2.c -o /tmp/sysmsg2

clean:
	rm -f $(TARGETS)
