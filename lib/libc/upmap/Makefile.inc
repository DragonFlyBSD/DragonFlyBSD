# Makefile.inc
#

.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/upmap ${.CURDIR}/../libc/upmap

CFLAGS+=-I${.CURDIR}/../libc/sysvipc

CMAPS+=	${.CURDIR}/upmap/Symbol.map

SRCS+=	upmap.c
SRCS+=	ukp_clock.c ukp_getpid.c ukp_setproctitle.c ukp_setname.c
SRCS+=	ukp_gettimeofday.c ukp_blocksigs.c

MAN+=	sigblockall.3
MLINKS+=sigblockall.3 sigunblockall.3
