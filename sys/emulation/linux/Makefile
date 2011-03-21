# $FreeBSD: src/sys/modules/linux/Makefile,v 1.34.2.12 2003/01/02 20:41:34 kan Exp $

.PATH: ${.CURDIR}/${MACHINE_ARCH}

ARCH=	arch_linux
KMOD=	linux
SRCS=	linux_dummy.c linux_emuldata.c linux_epoll.c \
	linux_file.c linux_futex.c linux_getcwd.c linux_ioctl.c \
	linux_ipc.c \
	linux_machdep.c linux_mib.c linux_misc.c linux_time.c linux_signal.c \
	linux_socket.c \
	linux_stats.c linux_sysctl.c linux_sysent.c linux_sysvec.c \
	linux_util.c opt_compat.h opt_global.h opt_vmpage.h
SRCS+=	bus_if.h device_if.h
SRCS+=	opt_nfs.h assym.s
OBJS=	linux_support.o linux_locore.o

SUBDIR= ${MACHINE_ARCH}
.if ${MACHINE_ARCH} == "i386"
SRCS+=	linux_ptrace.c linux_uid16.c imgact_linux.c opt_cpu.h
.endif

CLEANFILES=	linux_assym.h linux_genassym.o

linux_assym.h: linux_genassym.o

.if exists(@)
linux_assym.h: @/kern/genassym.sh
.endif
	sh @/kern/genassym.sh linux_genassym.o > ${.TARGET}

linux_locore.o: linux_locore.s linux_assym.h
	${CC} -c -x assembler-with-cpp -DLOCORE ${CFLAGS} \
	    ${.IMPSRC} -o ${.TARGET}

linux_support.o: linux_support.s linux_assym.h assym.s
	${CC} -c -x assembler-with-cpp -DLOCORE ${CFLAGS} \
	    ${.IMPSRC} -o ${.TARGET}

linux_genassym.o: linux_genassym.c linux.h @
	${CC} -c ${CFLAGS:N-fno-common} ${.IMPSRC}

opt_compat.h:
	echo "#define COMPAT_43 1" > opt_compat.h

.include <bsd.kmod.mk>
