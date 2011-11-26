LIB=	kinfo
SRCS=	kinfo_file.c kinfo_net.c kinfo_sched.c kinfo_tty.c kinfo_vfs.c
SRCS+=	kinfo_pcpu.c
INCS=	kinfo.h kinfo_pcpu.h
NOMAN=

CFLAGS+=	-I${.CURDIR}

SHLIB_MAJOR?= 2

.include <bsd.lib.mk>
