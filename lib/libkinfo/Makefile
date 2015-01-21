LIB=	kinfo
SRCS=	kinfo_file.c kinfo_net.c kinfo_sched.c kinfo_tty.c kinfo_vfs.c
SRCS+=	kinfo_pcpu.c
INCS=	kinfo.h kinfo_pcpu.h
MAN=	kinfo.3

CFLAGS+=	-I${.CURDIR}

SHLIB_MAJOR?= 2

MLINKS=	kinfo.3 kinfo_get_cpus.3 \
	kinfo.3 kinfo_get_files.3 \
	kinfo.3 kinfo_get_maxfiles.3 \
	kinfo.3 kinfo_get_net_rtstatistics.3 \
	kinfo.3 kinfo_get_openfiles.3 \
	kinfo.3 kinfo_get_sched_cputime.3 \
	kinfo.3 kinfo_get_sched_hz.3 \
	kinfo.3 kinfo_get_sched_profhz.3 \
	kinfo.3 kinfo_get_sched_stathz.3 \
	kinfo.3 kinfo_get_tty_tk_nin.3 \
	kinfo.3 kinfo_get_tty_tk_nout.3 \
	kinfo.3 kinfo_get_vfs_bufspace.3

.include <bsd.lib.mk>
