#
# $DragonFly: src/test/sysperf/Makefile,v 1.7 2004/04/10 21:36:52 dillon Exp $
#

TARGETS=/tmp/sc1 /tmp/sc2 /tmp/sc3 /tmp/sc4 \
	/tmp/loop1 /tmp/loop2 /tmp/loop3 \
	/tmp/call1 /tmp/call2 /tmp/call3 \
	/tmp/mt2 \
	/tmp/fork1 /tmp/pipe1 /tmp/pipe2 \
	/tmp/sw1 /tmp/sw2 /tmp/sw3 \
	/tmp/mbw1 \
	/tmp/upc1 \
	/tmp/exec1 /tmp/exec2

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

/tmp/pipe2: pipe2.c blib.c
	$(CC) $(CFLAGS) pipe2.c blib.c -o /tmp/pipe2

/tmp/loop1: loop1.c blib.c
	$(CC) $(CFLAGS) loop1.c blib.c -o /tmp/loop1

/tmp/loop2: loop2.c blib.c
	$(CC) $(CFLAGS) loop2.c blib.c -o /tmp/loop2

/tmp/loop3: loop3.c blib.c
	$(CC) $(CFLAGS) loop3.c blib.c -o /tmp/loop3

/tmp/call1: call1.c blib.c
	$(CC) $(CFLAGS) call1.c blib.c -o /tmp/call1

/tmp/call2: call2.c blib.c
	$(CC) $(CFLAGS) call2.c blib.c -o /tmp/call2

/tmp/call3: call3.c blib.c
	$(CC) $(CFLAGS) call3.c blib.c -o /tmp/call3

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

/tmp/upc1: upcall.S upcall1.c blib.c
	$(CC) $(CFLAGS) -DUSE_CALLU2 upcall.S upcall1.c blib.c -o /tmp/upc1

/tmp/exec1: exec1.c blib.c
	$(CC) $(CFLAGS) -static -DISSTATIC exec1.c blib.c -o /tmp/exec1

/tmp/exec2: exec1.c blib.c
	$(CC) $(CFLAGS) exec1.c blib.c -o /tmp/exec2

clean:
	rm -f $(TARGETS)
