#
# $DragonFly: src/test/sysperf/Makefile,v 1.3 2003/11/19 00:59:19 dillon Exp $
#

TARGETS=/tmp/sc1 /tmp/sc2 /tmp/sc3 /tmp/sc4 \
	/tmp/loop1 /tmp/loop2 /tmp/loop3 \
	/tmp/mt2 \
	/tmp/fork1 /tmp/pipe1 \
	/tmp/sw1 /tmp/sw2 /tmp/sw3 \
	/tmp/mbw1

CFLAGS= -O2 -g -I/usr/src/sys

all:	$(TARGETS)

/tmp/sc1: syscall1.c getuid_msg.c blib.c
	$(CC) $(CFLAGS) syscall1.c blib.c -o /tmp/sc1

/tmp/sc2: syscall2.c getuid_msg.c blib.c
	$(CC) $(CFLAGS) syscall2.c getuid_msg.c blib.c -o /tmp/sc2

/tmp/sc3: syscall3.c getuid_msg.c blib.c
	$(CC) $(CFLAGS) syscall3.c getuid_msg.c blib.c -o /tmp/sc3

/tmp/sc4: syscall4.c blib.c
	$(CC) $(CFLAGS) syscall4.c blib.c -o /tmp/sc4

/tmp/fork1: fork1.c blib.c
	$(CC) $(CFLAGS) fork1.c blib.c -o /tmp/fork1

/tmp/pipe1: pipe1.c blib.c
	$(CC) $(CFLAGS) pipe1.c blib.c -o /tmp/pipe1

/tmp/loop1: loop1.c blib.c
	$(CC) $(CFLAGS) loop1.c blib.c -o /tmp/loop1

/tmp/loop2: loop2.c blib.c
	$(CC) $(CFLAGS) loop2.c blib.c -o /tmp/loop2

/tmp/loop3: loop3.c blib.c
	$(CC) $(CFLAGS) loop3.c blib.c -o /tmp/loop3

/tmp/mbw1: mbwtest.c
	$(CC) -O mbwtest.c -o /tmp/mbw1

/tmp/mt2: mutex2.c blib.c mtx.s
	$(CC) $(CFLAGS) mutex2.c blib.c mtx.s -o /tmp/mt2

/tmp/sw1: quicksw1.c blib.c sw.S
	$(CC) $(CFLAGS) -DUSE_ALL quicksw1.c blib.c sw.S -o /tmp/sw1

/tmp/sw2: quicksw1.c blib.c sw.S
	$(CC) $(CFLAGS) -DUSE_CALLU1 quicksw1.c blib.c sw.S -o /tmp/sw2

/tmp/sw3: quicksw1.c blib.c sw.S
	$(CC) $(CFLAGS) -DUSE_CALLU2 quicksw1.c blib.c sw.S -o /tmp/sw3

clean:
	rm -f $(TARGETS)
