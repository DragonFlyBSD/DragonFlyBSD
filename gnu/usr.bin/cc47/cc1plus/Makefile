GCC_LANG_DIR=	gcc/cp
.include "../Makefile.langs"

PROG=		cc1plus
NOMAN=		1
NOSHARED?=	yes

# The cnfs.gperf file has a bug in the generator that is masked when built
# with std=gnu89.  Since gcc is built with this standard by default, the
# gcc devs have never bothered to fix it.

CSTD=		gnu89

# The compiler itself, modified from gcc/cp/Make-lang.in
# Currently CXX_TARGET_OBJS is the same as C_TARGTET_OBJS, but to ensure this
# code is future proof, rebuild the object files instead of using the object
# files at ../cc1
COMMOBJS=	${C_COMMON_OBJS:S/^/..\/cc1\//:.c=.o} \
		../cc1/main.o \
		../cc1/stub-objc.o \
		../cc1/tree-mudflap.o
CXX_C_SRC=	attribs.c ${CXX_TARGET_OBJS}

# Language-specific object files for C++ and Objective C++.
CXX_AND_OBJCXX_OBJS= \
	call.c \
	decl.c \
	expr.c \
	pt.c \
	typeck2.c \
	class.c \
	decl2.c \
	error.c \
	lex.c \
	parser.c \
	ptree.c \
	rtti.c \
	typeck.c \
	cvt.c \
	except.c \
	friend.c \
	init.c \
	method.c \
	search.c \
	semantics.c \
	tree.c \
	repo.c \
	dump.c \
	optimize.c \
	mangle.c \
	cp-objcp-common.c \
	name-lookup.c \
	cxx-pretty-print.c \
	cp-gimplify.c
FULLSRC=	${CXX_C_SRC} ${CXX_AND_OBJCXX_OBJS} cp-lang.c
SRCS=		${FULLSRC}
OBJS=		${COMMOBJS} cc1plus-checksum.o

BACKEND= ${LIBBACKEND} ${LIBCOMMONTARG} ${LIBCOMMON} ${LIBCPP} ${LIBDECNUMBER}
LDADD=   ${BACKEND} ${LIBIBERTY} ${BACKENDLIBS}
DPADD=   ${BACKEND} ${LIBIBERTY} ${BACKENDLIBS}

CFLAGS+= -DIN_GCC_FRONTEND

checksum-options:
	echo "${LD} ${LDFLAGS}" > ${.TARGET} 

cc1plus-checksum.o: ${COMMOBJS} ${FULLSRC:.c=.o} checksum-options
	${TOOLDIR}/genchecksum.nx ${COMMOBJS} ${FULLSRC:.c=.o} \
	    ${BACKEND} checksum-options > ${.TARGET:.o=.c}
	${CC} ${CFLAGS} -c ${.TARGET:.o=.c}

CLEANFILES+=	cc1plus-checksum.* checksum-options

.include <bsd.prog.mk>
