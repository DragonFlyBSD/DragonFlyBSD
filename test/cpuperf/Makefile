#
# $DragonFly: src/test/cpuperf/Makefile,v 1.1 2004/02/09 18:08:54 dillon Exp $
#

.PATH: ${.CURDIR}/../sysperf
TARGETS=/tmp/cpu_add  /tmp/cpu_cmpadd /tmp/cpu_cmpexg /tmp/cpu_lcmpexg \
	/tmp/cpu_call

CFLAGS= -O2 -g -I../sysperf

all:	$(TARGETS)

/tmp/cpu_add: cputest.c cpu_add.S blib.c
	$(CC) $(CFLAGS) ${.ALLSRC} -o ${.TARGET}

/tmp/cpu_cmpadd: cputest.c cpu_cmpadd.S blib.c
	$(CC) $(CFLAGS) ${.ALLSRC} -o ${.TARGET}

/tmp/cpu_cmpexg: cputest.c cpu_cmpexg.S blib.c
	$(CC) $(CFLAGS) ${.ALLSRC} -o ${.TARGET}

/tmp/cpu_lcmpexg: cputest.c cpu_lcmpexg.S blib.c
	$(CC) $(CFLAGS) ${.ALLSRC} -o ${.TARGET}

/tmp/cpu_call: cputest.c cpu_call.S blib.c
	$(CC) $(CFLAGS) ${.ALLSRC} -o ${.TARGET}

clean:
	rm -f $(TARGETS)
