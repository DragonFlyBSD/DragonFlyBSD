# If we defined SRCS, the mkdep fails because it evaluates files with .c
# extension with cc instead of c++ (go figure).  To avoid that, we need
# to define OBJS directly

TOP_PREFIX=	../../

.include "../../../Makefile.inc"
.include "../../../Makefile.langs"

PROG_CXX=	cc1
NOMAN=		yes
NOSHARED?=	yes

C_OBJS=		c/c-lang.o c/stub-objc.o
CXXXX_OBJS=	${C_OBJS:T}
OBJS=		${CXXXX_OBJS} cc1-checksum.o
SRCS=

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

cc1-checksum.cc: ${CXXXX_OBJS} ${GUTS} ${BACKEND} checksum-options
	${TOOLDIR}/genchecksum.nx ${CXXXX_OBJS} ${GUTS} ${BACKEND} \
	    checksum-options > ${.TARGET}

cc1-checksum.o: cc1-checksum.cc

CLEANFILES=	cc1-checksum.* checksum-options

.include <bsd.prog.mk>
