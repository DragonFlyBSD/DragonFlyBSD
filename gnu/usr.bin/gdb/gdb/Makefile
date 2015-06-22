PROG=	gdb
SRCS=	gdb.c
MAN=	gdb.1 gdbint.7 stabs.7

CFLAGS+= -I${.OBJDIR}/../libgdb -I${.CURDIR}/../libgdb -I${.CURDIR}/..
CFLAGS+= -I${BASEDIR}/include
CFLAGS+= -I${BASEDIR}/bfd -I${BASEDIR}/gdb
CFLAGS+= -I${BASEDIR}/gdb/common -I${.CURDIR}/../libbfd
CFLAGS+= -I${.OBJDIR}/../libbfd

DPADD=  ${GDBLIBS}
DPADD+= ${LIBNCURSES} ${LIBM} ${LIBZ} ${LIBKVM} ${LIBLZMA}
LDADD=  -L../libgdb -lgdb
LDADD+= -L../libreadline -lreadline
LDADD+= -L../libopcodes -lopcodes
LDADD+= -L../libbfd -lbfd
LDADD+= -L../libiberty -liberty
LDADD+= -L../libdecnumber -ldecnumber
LDADD+= -L../libgnu -lgnu
LDADD+= -lncurses -lm -lz -lkvm -llzma

.include <bsd.prog.mk>

.PATH:	${BASEDIR}/gdb
