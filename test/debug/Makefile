# Debug programs Makefile

TARGETS=  buserr revoke fdpassing posixlock seekbench umtx vmpageinfo
TARGETS+= wildcardinfo kmapinfo kwrite kshowprocs mbufinfo ncptrace
TARGETS+= ttyinfo vnodeinfo zallocinfo evfilt_user bufqueues testvblank

CFLAGS= -O0 -Wall -g -I../../sys -I../../sys/dev/drm/include
LDFLAGS= -lkvm -lpthread

all: ${TARGETS}

.for EACH in ${TARGETS}

${EACH}:
	${CC} ${CFLAGS} ${EACH}.c ${LDFLAGS} -o /tmp/${EACH}

.endfor

clean:
.for EACH in ${TARGETS}
	rm -f /tmp/${EACH}
.endfor
