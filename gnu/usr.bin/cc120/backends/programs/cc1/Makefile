.include "../../../Makefile.inc"

PROG_CXX=	cc1
NOMAN=		yes
NOSHARED?=	yes

# Language-specific object files for C.
# Derived from gcc/c/Make-lang.in, removed $(C_AND_OBJC_OBJS) since they're
# provided by ${GUTS}.
C_OBJS = c/c-lang.o c-family/stub-objc.o # $(C_AND_OBJC_OBJS)

SRCS=		${C_OBJS:T:.o=.cc}
# Skip calling 'mkdep' on the to-be-generated 'cc1-checksum.cc'.
.if !make(depend)
SRCS+=		cc1-checksum.cc
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

cc1-checksum.cc: ${C_OBJS:T} ${GUTS} ${BACKEND} checksum-options
	${TOOLDIR}/genchecksum.nx ${C_OBJS:T} ${GUTS} ${BACKEND} \
	    checksum-options > ${.TARGET}

CLEANFILES=	cc1-checksum.cc checksum-options

.include <bsd.prog.mk>
