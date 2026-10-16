GCC_LANG_DIR=	gcc/cp
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

PROG_CXX=	cc1plus
NOMAN=		yes
NOSHARED?=	yes

CXX_OBJS=	cp/cp-lang.o cp/stub-objc.o
CXX_OBJS_T=	${CXX_OBJS:T}

SRCS=		${CXX_OBJS_T:.c=.c}
# Skip calling 'mkdep' on the to-be-generated 'cc1plus-checksum.c'.
.if !make(depend)
SRCS+=		cc1plus-checksum.c
.endif

GUTS=		../../guts/guts-cxx/libguts-cxx.a \
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

cc1plus-checksum.c: ${CXX_OBJS_T} ${GUTS} ${BACKEND} checksum-options
	${TOOLDIR}/genchecksum.nx ${CXX_OBJS_T} ${GUTS} ${BACKEND} \
	    checksum-options > ${.TARGET}

CLEANFILES=	cc1plus-checksum.c checksum-options

.include <bsd.prog.mk>

# hack to force c++ compiler to compile *.c files
MKDEPCC:=	${CXX}
CC:=		${CXX}
CFLAGS:=	${CXXFLAGS}
