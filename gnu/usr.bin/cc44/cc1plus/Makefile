GCC_LANG_DIR=	gcc/cp

CFLAGS+=	-DTARGET_MACHINE=\"${target_machine}\"
CFLAGS+=	-DHOST_MACHINE=\"${target_machine}\"

PROG=	cc1plus
NOMAN=	1

# verbatim from gcc/cp/Make-lang.in
# The compiler itself.
# Shared with C front end:
CXX_C_OBJS = attribs.o c-common.o c-format.o c-pragma.o c-semantics.o c-lex.o \
	c-dump.o $(CXX_TARGET_OBJS) c-pretty-print.o c-opts.o c-pch.o \
	incpath.o cppdefault.o c-ppoutput.o c-cppbuiltin.o prefix.o \
	c-gimplify.o c-omp.o tree-inline.o

# Language-specific object files for C++ and Objective C++.
CXX_AND_OBJCXX_OBJS = cp/call.o cp/decl.o cp/expr.o cp/pt.o cp/typeck2.o \
 cp/class.o cp/decl2.o cp/error.o cp/lex.o cp/parser.o cp/ptree.o cp/rtti.o \
 cp/typeck.o cp/cvt.o cp/except.o cp/friend.o cp/init.o cp/method.o \
 cp/search.o cp/semantics.o cp/tree.o cp/repo.o cp/dump.o cp/optimize.o \
 cp/mangle.o cp/cp-objcp-common.o cp/name-lookup.o cp/cxx-pretty-print.o \
 cp/cp-gimplify.o tree-mudflap.o $(CXX_C_OBJS)

# Language-specific object files for C++.
CXX_OBJS = cp/cp-lang.o stub-objc.o $(CXX_AND_OBJCXX_OBJS)


SRCS=	${CXX_OBJS:T:.o=.c}
SRCS+=	main.c
SRCS+=	${GCC_CPU}-c.c		# this is CXX_TARGET_OBJS

LDADD+=	${LIBBACKEND} ${BACKENDLIBS}
DPADD+=	${LIBBACKEND} ${BACKENDLIBS}

OBJS+=	cc1plus-checksum.o

.include <bsd.prog.mk>

# compiler checksum handling
CLEANFILES+=	cc1plus-checksum.c dummy-checksum.o cc1plus-dummy

cc1plus-dummy: ${OBJS:Ncc1plus-checksum.o} dummy-checksum.o
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC} ${LDADD}

cc1plus-checksum.c: cc1plus-dummy
	${TOOLDIR}/genchecksum.nx ${.ALLSRC} > ${.TARGET}


.include "../Makefile.langs"
