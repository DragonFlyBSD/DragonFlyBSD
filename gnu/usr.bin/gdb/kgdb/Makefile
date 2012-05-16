PROG=	kgdb
SRCS=	kgdb.c kthr.c kld.c trgt.c trgt_${MACHINE_ARCH}.c

CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${BASEDIR}/include
CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb -I${BASEDIR}/gdb/config -I${.CURDIR}/../libbfd
CFLAGS+= -I${.OBJDIR}/../libbfd -I${BASEDIR}/gdb/common
CFLAGS+= -D_KERNEL_STRUCTURES

DPADD=  ${GDBLIBS}
DPADD+= ${LIBNCURSES} ${LIBM} ${LIBZ} ${LIBKVM}
LDADD=  -L../libgdb -lgdb
LDADD+= -L../libreadline -lreadline
LDADD+= -L../libopcodes -lopcodes
LDADD+= -L../libbfd -lbfd
LDADD+= -L../libiberty -liberty
LDADD+= -L../libdecnumber -ldecnumber
LDADD+= -lncurses -lm -lz -lkvm

WARNS?=	2

.include <bsd.prog.mk>

.PATH:	${BASEDIR}/gdb
