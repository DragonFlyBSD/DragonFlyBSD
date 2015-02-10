# If we defined SRCS, the mkdep fails because it evaluates files with .c
# extension with cc instead of c++ (go figure).  To avoid that, we need
# to define OBJS directly

GCC_LANG_DIR=	gcc/objc
TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

PROG_CXX=	cc1obj
NOMAN=		yes
NOSHARED?=	yes

CXXXX_OBJS=	${OBJC_OBJS:T:c.=o}
OBJS=		${CXXXX_OBJS} cc1obj-checksum.o
SRCS=

GUTS=		../../guts/guts-cobjc/libguts-cobjc.na \
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

cc1obj-checksum.c: ${CXXXX_OBJS} ${GUTS} ${BACKEND} checksum-options
	${TOOLDIR}/genchecksum.nx ${CXXXX_OBJS} ${GUTS} ${BACKEND} \
	    checksum-options > ${.TARGET}

# hack to force c++ compiler to compile *.c files to create library
.for ofile in ${OBJS}
${ofile}: ${ofile:.o=.c}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

CLEANFILES=	cc1obj-checksum.* checksum-options

.include <bsd.prog.mk>
