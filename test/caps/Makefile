# $DragonFly: src/test/caps/Makefile,v 1.3 2004/01/18 12:39:56 dillon Exp $
#

TARGETS=/tmp/thr1 /tmp/thr2 /tmp/thr3 /tmp/caps_s /tmp/caps_c

CFLAGS= -O2 -g -I/usr/src/lib -I/usr/src/lib/libcaps/${MACHINE_ARCH} -DSMP

all:	$(TARGETS)

#/tmp/caps1: caps1.c
#	$(CC) $(CFLAGS) caps1.c -lcaps -o /tmp/caps1

/tmp/thr1: thr1.c
	$(CC) $(CFLAGS) thr1.c -lcaps -o /tmp/thr1

/tmp/thr2: thr2.c
	$(CC) $(CFLAGS) thr2.c -lcaps -o /tmp/thr2

/tmp/thr3: thr3.c
	$(CC) $(CFLAGS) thr3.c -lcaps -o /tmp/thr3

/tmp/caps_s: server.c
	$(CC) $(CFLAGS) -DDEBUG server.c -o /tmp/caps_s

/tmp/caps_c: client.c
	$(CC) $(CFLAGS) -DDEBUG client.c -o /tmp/caps_c

clean:
	rm -f $(TARGETS)
