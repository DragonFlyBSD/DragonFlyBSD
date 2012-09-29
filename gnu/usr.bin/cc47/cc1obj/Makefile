GCC_LANG_DIR=	gcc/objc
.include "../Makefile.langs"

PROG=		cc1obj
NOMAN=		1
NOSHARED?=	yes

# verbatim from gcc/objc/Make-lang.in
OBJC_OBJS= \
	objc-act.c \
	objc-encoding.c \
	objc-gnu-runtime-abi-01.c \
	objc-lang.c \
	objc-map.c \
	objc-next-runtime-abi-01.c \
	objc-next-runtime-abi-02.c \
	objc-runtime-shared-support.c

SRCS=		${OBJC_OBJS}
COMMOBJS=	${C_AND_OBJC_OBJS:S/^/..\/cc1\//:.c=.o} ../cc1/main.o
OBJS=		${COMMOBJS} cc1obj-checksum.o

BACKEND= ${LIBBACKEND} ${LIBCOMMONTARG} ${LIBCOMMON} ${LIBCPP} ${LIBDECNUMBER}
LDADD=   ${BACKEND} ${LIBIBERTY} ${BACKENDLIBS}
DPADD=   ${BACKEND} ${LIBIBERTY} ${BACKENDLIBS}

checksum-options:
	echo "${LD} ${LDFLAGS}" > ${.TARGET} 

cc1obj-checksum.o: ${COMMOBJS} ${OBJC_OBJS:.c=.o} checksum-options
	${TOOLDIR}/genchecksum.nx ${COMMOBJS} ${OBJC_OBJS:.c=.o} \
	    ${BACKEND} checksum-options > ${.TARGET:.o=.c}
	${CC} ${CFLAGS} -c ${.TARGET:.o=.c}

CLEANFILES+= cc1obj-checksum.* checksum-options

.include <bsd.prog.mk>
