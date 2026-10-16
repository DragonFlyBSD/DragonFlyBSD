TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

PROG_CXX=	cc1
NOMAN=		yes
NOSHARED?=	yes

C_OBJS=		c/c-lang.o c/stub-objc.o
C_OBJS_T=	${C_OBJS:T}

SRCS=		${C_OBJS_T:.o=.c}
# Skip calling 'mkdep' on the to-be-generated 'cc1-checksum.c'.
.if !make(depend)
SRCS+=		cc1-checksum.c
.endif

GUTS=		../../guts/guts-c/libguts-c.a \
		../../guts/guts-common/libguts-common.a \
		../../guts/guts-target/libguts-target.a
BACKEND=	${LIBBACKEND} ${LIBCOMMONTARG} ${LIBCOMMON} \
		${LIBCPP} ${LIBDECNUMBER}
LIBS=		${LIBCOMMON} ${LIBCPP} ${LIBBACKTRACE} ${LIBIBERTY} \
		${LIBDECNUMBER}
LDADD=		${GUTS} ${BACKEND} ${LIBS} ${BACKENDLIBS}
DPADD=		${GUTS} ${BACKEND} ${LIBS} ${BACKENDLIBS}

checksum-options:
	echo "${LD} ${LDFLAGS}" > ${.TARGET}

cc1-checksum.c: ${C_OBJS_T} ${GUTS} ${BACKEND} checksum-options
	${TOOLDIR}/genchecksum.nx ${C_OBJS_T} ${GUTS} ${BACKEND} \
	    checksum-options > ${.TARGET}

CLEANFILES=	cc1-checksum.c checksum-options

.include <bsd.prog.mk>

# hack to force c++ compiler to compile *.c files
MKDEPCC:=	${CXX}
CC:=		${CXX}
CFLAGS:=	${CXXFLAGS}
