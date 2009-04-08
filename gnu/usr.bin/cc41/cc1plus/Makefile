# $DragonFly: src/gnu/usr.bin/cc41/cc1plus/Makefile,v 1.2 2007/01/20 03:51:33 corecode Exp $

GCC_LANG_DIR=	gcc/cp

CONTRIBDIR=	${GCCDIR}/gcc/cp

CFLAGS+=	-DTARGET_MACHINE=\"${target_machine}\"
CFLAGS+=	-DHOST_MACHINE=\"${target_machine}\"

PROG=	cc1plus
NOMAN=	1


SRCS=	cp-lang.c stub-objc.c call.c decl.c expr.c pt.c typeck2.c \
	class.c decl2.c error.c lex.c parser.c ptree.c rtti.c \
	typeck.c cvt.c except.c friend.c init.c method.c \
	search.c semantics.c tree.c repo.c dump.c optimize.c \
	mangle.c cp-objcp-common.c name-lookup.c cxx-pretty-print.c \
	cp-gimplify.c tree-mudflap.c

SRCS+=	attribs.c c-common.c c-format.c c-pragma.c c-semantics.c c-lex.c \
        c-dump.c c-pretty-print.c c-opts.c c-pch.c \
        c-incpath.c cppdefault.c c-ppoutput.c c-cppbuiltin.c prefix.c \
        c-gimplify.c tree-inline.c

SRCS+=	main.c

SRCS+=	tm-preds.h tree-check.h genrtl.h insn-flags.h insn-constants.h
SRCS+=	gtype-desc.h insn-modes.h insn-config.h options.h insn-modes.c
SRCS+=	insn-emit.c insn-codes.h insn-attrtab.c insn-attr.h insn-extract.c
SRCS+=	insn-opinit.c

LDADD+=	${LIBBACKEND} ${LIBCPP}
DPADD+=	${LIBBACKEND} ${LIBCPP}

CFLAGS+= -I. -I${CONTRIBDIR}

OBJS+=	cc1-checksum.o

.include <bsd.prog.mk>

# compiler checksum handling
CLEANFILES+=	cc1-checksum.c dummy-checksum.o cc1-dummy

cc1-dummy: ${OBJS:Ncc1-checksum.o} dummy-checksum.o
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC} ${LDADD}

cc1-checksum.c: cc1-dummy
	${TOOLDIR}/genchecksum/genchecksum.nx ${.ALLSRC} > ${.TARGET}


.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"
