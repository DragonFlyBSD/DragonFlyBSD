# $DragonFly: src/test/caps/Makefile,v 1.2 2003/12/07 04:26:41 dillon Exp $
#

TARGETS=/tmp/caps1 /tmp/thr1 /tmp/thr2 /tmp/thr3

CFLAGS= -O2 -g -I/usr/src/lib -I/usr/src/lib/libcaps/${MACHINE_ARCH} -DSMP

all:	$(TARGETS)

/tmp/caps1: caps1.c
	$(CC) $(CFLAGS) caps1.c -lcaps -o /tmp/caps1

/tmp/thr1: thr1.c
	$(CC) $(CFLAGS) thr1.c -lcaps -o /tmp/thr1

/tmp/thr2: thr2.c
	$(CC) $(CFLAGS) thr2.c -lcaps -o /tmp/thr2

/tmp/thr3: thr3.c
	$(CC) $(CFLAGS) thr3.c -lcaps -o /tmp/thr3

