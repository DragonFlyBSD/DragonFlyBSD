# $DragonFly: src/test/caps/Makefile,v 1.1 2003/11/24 21:15:59 dillon Exp $
#

TARGETS=/tmp/caps1

CFLAGS= -O2 -g -I/usr/src/lib

all:	$(TARGETS)

/tmp/caps1: caps1.c
	$(CC) $(CFLAGS) caps1.c -llwkt -o /tmp/caps1

