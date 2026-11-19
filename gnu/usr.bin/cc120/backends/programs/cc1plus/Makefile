GCC_LANG_DIR=	gcc/cp
.include "../../../Makefile.inc"

PROG_CXX=	cc1plus
NOMAN=		yes
NOSHARED?=	yes

# Language-specific object files for C++.
# Derived from gcc/cp/Make-lang.in, removed $(CXX_AND_OBJCXX_OBJS) since
# they're provided by ${GUTS}.
CXX_OBJS = cp/cp-lang.o c-family/stub-objc.o # $(CXX_AND_OBJCXX_OBJS)

SRCS=		${CXX_OBJS:T:.o=.cc}
# Skip calling 'mkdep' on the to-be-generated 'cc1plus-checksum.cc'.
.if !make(depend)
SRCS+=		cc1plus-checksum.cc
.endif

GUTS=		../../guts/guts-cxx/libguts-cxx.a \
		../../guts/guts-common/libguts-common.a \
		../../guts/guts-target/libguts-target.a
BACKEND=	${LIBBACKEND} ${LIBCOMMONTARG} ${LIBCOMMON} \
		${LIBCPP} ${LIBDECNUMBER}
LIBS=		${LIBCOMMON} ${LIBCPP} ${LIBBACKTRACE} ${LIBIBERTY} \
		${LIBDECNUMBER} ${LIBCODY}
LDADD=		${GUTS} ${BACKEND} ${LIBS} ${BACKENDLIBS}
DPADD=		${GUTS} ${BACKEND} ${LIBS} ${BACKENDLIBS}

checksum-options:
	echo "${LD} ${LDFLAGS}" > ${.TARGET}

cc1plus-checksum.cc: ${CXX_OBJS:T} ${GUTS} ${BACKEND} checksum-options
	${TOOLDIR}/genchecksum.nx ${CXX_OBJS:T} ${GUTS} ${BACKEND} \
	    checksum-options > ${.TARGET}

CLEANFILES=	cc1plus-checksum.cc checksum-options

.include <bsd.prog.mk>
