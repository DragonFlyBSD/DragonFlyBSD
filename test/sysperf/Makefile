TARGETS=/tmp/sc1 /tmp/sc2 /tmp/sc3 /tmp/sc4 /tmp/sc5 /tmp/sc6 /tmp/sc7 \
	/tmp/loop1 /tmp/loop2 /tmp/loop3 /tmp/loop4 \
	/tmp/call1 /tmp/call2 /tmp/call3 /tmp/cmp \
	/tmp/mt2 /tmp/mt3 /tmp/mt4 \
	/tmp/fork1 /tmp/pipe1 /tmp/pipe2 \
	/tmp/sp1 \
	/tmp/sw1 /tmp/sw2 /tmp/sw3 \
	/tmp/mbw1 \
	/tmp/exec1 /tmp/exec2 \
	/tmp/mem1 /tmp/mem2 \
	/tmp/cld1 \
	/tmp/rr1

.if defined(NO_OPTIMIZE)
COPTFLAGS= -O
.else
COPTFLAGS= -O2
.endif
CFLAGS= ${COPTFLAGS} -g -I/usr/src/sys

all:	$(TARGETS)

/tmp/cld1: cld1.c blib.c
	$(CC) $(CFLAGS) cld1.c blib.c -o /tmp/cld1

/tmp/sc1: syscall1.c blib.c
	$(CC) $(CFLAGS) syscall1.c blib.c -o /tmp/sc1

/tmp/sc2: syscall2.c blib.c
	$(CC) $(CFLAGS) syscall2.c blib.c -o /tmp/sc2

/tmp/sc3: syscall3.c blib.c
	$(CC) $(CFLAGS) syscall3.c blib.c -o /tmp/sc3

/tmp/sc4: syscall4.c blib.c
	$(CC) $(CFLAGS) syscall4.c blib.c -o /tmp/sc4

/tmp/sc5: syscall5.c blib.c
	$(CC) $(CFLAGS) syscall5.c blib.c -o /tmp/sc5

/tmp/sc6: syscall6.c blib.c
	$(CC) $(CFLAGS) syscall6.c blib.c -o /tmp/sc6

/tmp/sc7: syscall7.c blib.c
	$(CC) $(CFLAGS) syscall7.c blib.c -o /tmp/sc7

/tmp/fork1: fork1.c blib.c
	$(CC) $(CFLAGS) fork1.c blib.c -o /tmp/fork1

/tmp/pipe1: pipe1.c blib.c
	$(CC) $(CFLAGS) pipe1.c blib.c -o /tmp/pipe1

/tmp/pipe2: pipe2.c blib.c
	$(CC) $(CFLAGS) pipe2.c blib.c -o /tmp/pipe2

/tmp/sp1: socketpair.c blib.c
	$(CC) $(CFLAGS) socketpair.c blib.c -o /tmp/sp1

/tmp/loop1: loop1.c blib.c
	$(CC) $(CFLAGS) loop1.c blib.c -o /tmp/loop1

/tmp/loop2: loop2.c blib.c
	$(CC) $(CFLAGS) loop2.c blib.c -o /tmp/loop2

/tmp/loop3: loop3.c blib.c
	$(CC) $(CFLAGS) loop3.c blib.c -o /tmp/loop3

/tmp/loop4: loop4.c blib.c
	$(CC) $(CFLAGS) loop4.c blib.c -o /tmp/loop4

/tmp/call1: call1.c blib.c
	$(CC) $(CFLAGS) call1.c blib.c -o /tmp/call1

/tmp/call2: call2.c blib.c
	$(CC) $(CFLAGS) call2.c blib.c -o /tmp/call2

/tmp/call3: call3.c blib.c
	$(CC) $(CFLAGS) call3.c blib.c -o /tmp/call3

/tmp/cmp: cmp.c blib.c
	    $(CC) $(CFLAGS) cmp.c blib.c -o /tmp/cmp

/tmp/mbw1: mbwtest.c
	$(CC) -O mbwtest.c -o /tmp/mbw1

/tmp/mt1: mutex1.c blib.c mtx.s
	$(CC) $(CFLAGS) mutex1.c blib.c mtx.s -o /tmp/mt1

/tmp/mt2: mutex2.c blib.c mtx.s
	$(CC) $(CFLAGS) mutex2.c blib.c mtx.s -o /tmp/mt2

/tmp/mt3: mutex3.c blib.c mtx.s
	$(CC) $(CFLAGS) mutex3.c blib.c mtx.s -o /tmp/mt3

/tmp/mt4: mutex4.c blib.c mtx.s
	$(CC) $(CFLAGS) mutex4.c blib.c mtx.s -o /tmp/mt4

/tmp/sw1: quicksw1.c blib.c sw.S
	$(CC) $(CFLAGS) -DUSE_ALL quicksw1.c blib.c sw.S -o /tmp/sw1

/tmp/sw2: quicksw1.c blib.c sw.S
	$(CC) $(CFLAGS) -DUSE_CALLU1 quicksw1.c blib.c sw.S -o /tmp/sw2

/tmp/sw3: quicksw1.c blib.c sw.S
	$(CC) $(CFLAGS) -DUSE_CALLU2 quicksw1.c blib.c sw.S -o /tmp/sw3

/tmp/exec1: exec1.c blib.c
	$(CC) $(CFLAGS) -static -DISSTATIC exec1.c blib.c -o /tmp/exec1

/tmp/exec2: exec1.c blib.c
	$(CC) $(CFLAGS) exec1.c blib.c -o /tmp/exec2

/tmp/mem1: memcpy.c blib.c
	$(CC) $(CFLAGS) memcpy.c blib.c -o /tmp/mem1

/tmp/mem2: memzero.c blib.c
	$(CC) $(CFLAGS) memzero.c blib.c -o /tmp/mem2

/tmp/read1: read1.c blib.c
	$(CC) $(CFLAGS) read1.c blib.c -o /tmp/read1

/tmp/rr1: randread.c blib.c
	$(CC) $(CFLAGS) randread.c blib.c -o /tmp/rr1

clean:
	rm -f $(TARGETS)
