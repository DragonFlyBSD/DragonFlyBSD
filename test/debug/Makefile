# Debug programs Makefile

TARGETS=  buserr revoke fdpassing posixlock seekbench umtx vmpageinfo wildcardinfo
TARGETS+= kmapinfo kwrite kshowprocs mbufinfo ncptrace ttyinfo vnodeinfo zallocinfo
TARGETS+= evfilt_user bufqueues

CFLAGS= -O0 -g -I ../../sys

all: ${TARGETS}

bufqueues:
	$(CC) $(CFLAGS) bufqueues.c -lkvm -o ${.TARGET}

buserr:
	$(CC) $(CFLAGS) buserr.c -o ${.TARGET}

evfilt_user:
	$(CC) $(CFLAGS) evfilt_user.c -lpthread -o ${.TARGET}

fdpassing:
	$(CC) $(CFLAGS) fdpassing.c -o ${.TARGET}

kmapinfo:
	$(CC) $(CFLAGS) kmapinfo.c -lkvm -o ${.TARGET}

kshowprocs:
	$(CC) $(CFLAGS) kshowprocs.c -lkvm -o ${.TARGET}

kwrite:
	$(CC) $(CFLAGS) kwrite.c -lkvm -o ${.TARGET}

mbufinfo:
	$(CC) $(CFLAGS) mbufinfo.c -lkvm -o ${.TARGET}

ncptrace:
	$(CC) $(CFLAGS) ncptrace.c -lkvm -o ${.TARGET}

posixlock:
	$(CC) $(CFLAGS) posixlock.c -o ${.TARGET}

revoke:
	$(CC) $(CFLAGS) revoke.c -o ${.TARGET}

seekbench:
	$(CC) $(CFLAGS) seekbench.c -o ${.TARGET}

ttyinfo:
	$(CC) $(CFLAGS) ttyinfo.c -lkvm -o ${.TARGET}

umtx:
	$(CC) $(CFLAGS) umtx.c -o ${.TARGET}

vmpageinfo:
	$(CC) $(CFLAGS) vmpageinfo.c -lkvm -o ${.TARGET}

vnodeinfo:
	$(CC) $(CFLAGS) vnodeinfo.c -lkvm -o ${.TARGET}

wildcardinfo:
	$(CC) $(CFLAGS) wildcardinfo.c -lkvm -o ${.TARGET}

zallocinfo:
	$(CC) $(CFLAGS) zallocinfo.c -lkvm -o ${.TARGET}

clean:
	rm -f $(TARGETS)
