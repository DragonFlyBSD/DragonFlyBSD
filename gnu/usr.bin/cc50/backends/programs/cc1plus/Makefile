# If we defined SRCS, the mkdep fails because it evaluates files with .c
# extension with cc instead of c++ (go figure).  To avoid that, we need
# to define OBJS directly

GCC_LANG_DIR=	gcc/cp
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

PROG_CXX=	cc1plus
NOMAN=		yes
NOSHARED?=	yes

CXX_OBJS=	cp/cp-lang.o cp/stub-objc.o
CXXXX_OBJS=	${CXX_OBJS:T}
OBJS=		${CXXXX_OBJS} cc1plus-checksum.o
SRCS=		

GUTS=		../../guts/guts-cxx/libguts-cxx.na \
		../../guts/guts-common/libguts-common.na \
		../../guts/guts-target/libguts-target.na
BACKEND=	${LIBBACKEND} ${LIBCOMMONTARG} ${LIBCOMMON} \
		${LIBCPP} ${LIBDECNUMBER}
LIBS=		${LIBCOMMON} ${LIBCPP} ${LIBBACKTRACE} ${LIBIBERTY} \
		${LIBDECNUMBER}
LDADD=		${GUTS} ${BACKEND} ${LIBS} ${BACKENDLIBS}
DPADD=		${GUTS} ${BACKEND} ${LIBS} ${BACKENDLIBS}

checksum-options:
	echo "${LD} ${LDFLAGS}" > ${.TARGET} 

cc1plus-checksum.c: ${CXXXX_OBJS} ${GUTS} ${BACKEND} checksum-options
	${TOOLDIR}/genchecksum.nx ${CXXXX_OBJS} ${GUTS} ${BACKEND} \
	    checksum-options > ${.TARGET}

# hack to force c++ compiler to compile *.c files to create library
.for ofile in ${OBJS}
${ofile}: ${ofile:.o=.c}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

CLEANFILES=	cc1plus-checksum.* checksum-options

.include <bsd.prog.mk>
