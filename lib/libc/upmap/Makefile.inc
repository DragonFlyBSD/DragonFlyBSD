# Makefile.inc
#

.PATH: ${.CURDIR}/../libc/upmap

CMAPS+=	${.CURDIR}/upmap/Symbol.map

SRCS+=	upmap.c
SRCS+=	ukp_clock.c ukp_getpid.c ukp_setproctitle.c ukp_setname.c
SRCS+=	ukp_gettimeofday.c ukp_blocksigs.c

MAN+=	sigblockall.3
MLINKS+=sigblockall.3 sigunblockall.3
