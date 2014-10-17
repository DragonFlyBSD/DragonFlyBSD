# Makefile.inc
#

.PATH: ${.CURDIR}/../libc/${MACHINE_ARCH}/upmap ${.CURDIR}/../libc/upmap

CFLAGS+=-I${.CURDIR}/../libc/sysvipc

CMAPS+=	${.CURDIR}/upmap/Symbol.map

SRCS+=	upmap.c ukp_clock.c ukp_getpid.c ukp_setproctitle.c
