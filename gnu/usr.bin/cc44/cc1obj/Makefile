GCC_LANG_DIR=	gcc/objc

CFLAGS+=	-DTARGET_MACHINE=\"${target_machine}\"
CFLAGS+=	-DHOST_MACHINE=\"${target_machine}\"

PROG=	cc1obj
NOMAN=	1

# verbatim from gcc/Makefile and gcc/objc/Make-lang.in
# Language-specific object files for C and Objective C.
C_AND_OBJC_OBJS = attribs.o c-errors.o c-lex.o c-pragma.o c-decl.o c-typeck.o \
  c-convert.o c-aux-info.o c-common.o c-opts.o c-format.o c-semantics.o \
  c-ppoutput.o c-cppbuiltin.o \
  c-objc-common.o c-dump.o c-pch.o c-parser.o \
  c-gimplify.o tree-mudflap.o c-pretty-print.o c-omp.o

# Language-specific object files for Objective C.
OBJC_OBJS = objc/objc-lang.o objc/objc-act.o ${C_AND_OBJC_OBJS}


SRCS=	${OBJC_OBJS:T:.o=.c}
SRCS+=	main.c
SRCS+=	${GCC_CPU}-c.c		# this is C_TARGET_OBJS

LDADD+=	${LIBBACKEND} ${BACKENDLIBS}
DPADD+=	${LIBBACKEND} ${BACKENDLIBS}

OBJS+=	cc1obj-checksum.o

.include <bsd.prog.mk>

# compiler checksum handling
CLEANFILES+=	cc1obj-checksum.c dummy-checksum.o cc1obj-dummy

cc1obj-dummy: ${OBJS:Ncc1obj-checksum.o} dummy-checksum.o
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC} ${LDADD}

cc1obj-checksum.c: cc1obj-dummy
	${TOOLDIR}/genchecksum.nx ${.ALLSRC} > ${.TARGET}


.include "../Makefile.langs"
