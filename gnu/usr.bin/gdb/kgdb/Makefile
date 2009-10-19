PROG=	kgdb
SRCS=	kgdb.c kthr.c kld.c trgt.c trgt_${MACHINE_ARCH}.c

CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${BASEDIR}/include
CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb -I${BASEDIR}/gdb/config -I${.CURDIR}/../libbfd
CFLAGS+= -I${.OBJDIR}/../libbfd -I${BASEDIR}/gdb/common
CFLAGS+= -D_KERNEL_STRUCTURES

DPADD=	${LIBEDIT} ${LIBKVM} ${LIBTERMCAP} ${LIBM} ${LIBZ}
DPADD+=	${GDBLIBS}
LDADD=	-L../libgdb -lgdb -L../libbfd -lbfd -L../libopcodes -lopcodes
LDADD+= -L../libiberty -liberty -L../libdecnumber -ldecnumber
LDADD+=	-ledit -lkvm -ltermcap -lm -lz

WARNS?=	2

.include <bsd.prog.mk>

.PATH:	${BASEDIR}/gdb
