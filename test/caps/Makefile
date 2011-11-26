# $DragonFly: src/test/caps/Makefile,v 1.8 2004/08/18 09:10:30 dillon Exp $
#

# thr{1,2,3} disabled (obsolete at the moment)
TARGETS= /tmp/caps_s /tmp/caps_c /tmp/caps_e /tmp/caps_d

CFLAGS=	-O2 -g -DSMP
CFLAGS+=-I${.CURDIR}/../../lib -I${.CURDIR}/../../lib/libcaps/${MACHINE_ARCH}

all:	$(TARGETS)

#/tmp/caps1: caps1.c
#	$(CC) $(CFLAGS) caps1.c -lcaps -o /tmp/caps1
#

/tmp/thr1: thr1.c
	$(CC) $(CFLAGS) thr1.c -lcaps -o /tmp/thr1

/tmp/thr2: thr2.c
	$(CC) $(CFLAGS) thr2.c -lcaps -o /tmp/thr2

/tmp/thr3: thr3.c
	$(CC) $(CFLAGS) thr3.c -lcaps -o /tmp/thr3

/tmp/caps_s: server.c
	$(CC) $(CFLAGS) server.c -o /tmp/caps_s

/tmp/caps_c: client.c
	$(CC) $(CFLAGS) client.c -o /tmp/caps_c

/tmp/caps_e: encoder.c
	$(CC) $(CFLAGS) encoder.c -lcaps -o /tmp/caps_e

/tmp/caps_d: decoder.c
	$(CC) $(CFLAGS) decoder.c -lcaps -o /tmp/caps_d

clean:
	rm -f $(TARGETS)
