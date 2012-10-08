.include "../Makefile.langs"

PROG=		cc1
NOMAN=		1
NOSHARED?=	yes

C_OBJS=	\
	c-lang.c \
	stub-objc.c \
	${C_AND_OBJC_OBJS}

SRCS+=	${C_OBJS} main.c
OBJS+=	cc1-checksum.o

BACKEND= ${LIBBACKEND} ${LIBCOMMONTARG} ${LIBCOMMON} ${LIBCPP} ${LIBDECNUMBER}
LDADD= ${BACKEND} ${LIBIBERTY} ${BACKENDLIBS}
DPADD= ${BACKEND} ${LIBIBERTY} ${BACKENDLIBS}

checksum-options:
	echo "${LD} ${LDFLAGS}" > ${.TARGET} 

cc1-checksum.o: ${C_OBJS:.c=.o} ${BACKEND} checksum-options
	${TOOLDIR}/genchecksum.nx ${C_OBJS:.c=.o} ${BACKEND} \
	    checksum-options > ${.TARGET:.o=.c}
	${CC} ${CFLAGS} -c ${.TARGET:.o=.c}

CLEANFILES+= cc1-checksum.* checksum-options

.include <bsd.prog.mk>
