TARGETS=/tmp/sc1 /tmp/sc2 /tmp/sc3 /tmp/sc4 /tmp/sc5 /tmp/sc6 /tmp/sc7 \
	/tmp/loop1 /tmp/loop2 /tmp/loop3 /tmp/loop4 \
	/tmp/call1 /tmp/call2 /tmp/call3 /tmp/cmp \
	/tmp/mt2 /tmp/mt3 /tmp/mt4 \
	/tmp/fork1 /tmp/pipe1 /tmp/pipe2 /tmp/pipe3 \
	/tmp/umtx1 \
	/tmp/sp1 \
	/tmp/sw1 /tmp/sw2 /tmp/sw3 \
	/tmp/mbw1 \
	/tmp/exec1 /tmp/exec2 \
	/tmp/mem1 /tmp/mem2 \
	/tmp/cld1 \
	/tmp/rr1 \
	/tmp/lockmgr1 /tmp/lockmgr2 /tmp/lockmgr3 \
	/tmp/loop900k /tmp/pipe900k /tmp/sleep900k

.if defined(NO_OPTIMIZE)
COPTFLAGS= -O
.else
COPTFLAGS= -O2
.endif
CFLAGS= ${COPTFLAGS} -g -I/usr/src/sys

all:	${TARGETS}

/tmp/cld1: cld1.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/sc1: syscall1.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/sc2: syscall2.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/sc3: syscall3.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/sc4: syscall4.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/sc5: syscall5.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/sc6: syscall6.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/sc7: syscall7.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/fork1: fork1.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/pipe1: pipe1.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/pipe2: pipe2.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/pipe3: pipe3.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -pthread -o ${.TARGET}

/tmp/umtx1: umtx1.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -pthread -o ${.TARGET}

/tmp/sp1: socketpair.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/loop1: loop1.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/loop2: loop2.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/loop3: loop3.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/loop4: loop4.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/call1: call1.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/call2: call2.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/call3: call3.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/cmp: cmp.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/mbw1: mbwtest.c
	${CC} -O ${.ALLSRC} -o ${.TARGET}

/tmp/mt1: mutex1.c blib.c mtx.s
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/mt2: mutex2.c blib.c mtx.s
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/mt3: mutex3.c blib.c mtx.s
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/mt4: mutex4.c blib.c mtx.s
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/sw1: quicksw1.c blib.c sw.S
	${CC} ${CFLAGS} -DUSE_ALL ${.ALLSRC} -o ${.TARGET}

/tmp/sw2: quicksw1.c blib.c sw.S
	${CC} ${CFLAGS} -DUSE_CALLU1 ${.ALLSRC} -o ${.TARGET}

/tmp/sw3: quicksw1.c blib.c sw.S
	${CC} ${CFLAGS} -DUSE_CALLU2 ${.ALLSRC} -o ${.TARGET}

/tmp/exec1: exec1.c blib.c
	${CC} ${CFLAGS} -static -DISSTATIC ${.ALLSRC} -o ${.TARGET}

/tmp/exec2: exec1.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/mem1: memcpy.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/mem2: memzero.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/read1: read1.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/rr1: randread.c blib.c
	${CC} ${CFLAGS} ${.ALLSRC} -o ${.TARGET}

/tmp/lockmgr1: lockmgr1.c blib.c
	${CC} ${.ALLSRC} -o ${.TARGET}

/tmp/lockmgr2: lockmgr2.c blib.c
	${CC} ${.ALLSRC} -o ${.TARGET}

/tmp/lockmgr3: lockmgr3.c blib.c
	${CC} ${.ALLSRC} -o ${.TARGET}

/tmp/loop900k: loop900k.c
	${CC} ${CFLAGS} -static ${.ALLSRC} -o ${.TARGET}

/tmp/pipe900k: pipe900k.c
	${CC} ${CFLAGS} -static ${.ALLSRC} -o ${.TARGET}

/tmp/sleep900k: sleep900k.c
	${CC} ${CFLAGS} -static ${.ALLSRC} -o ${.TARGET}

clean:
	rm -f ${TARGETS}
