# $DragonFly: src/gnu/usr.bin/cc40/cc1/Attic/Makefile,v 1.1 2005/06/05 22:43:20 corecode Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

CFLAGS+=	-DTARGET_MACHINE=\"${target}\" -DHOST_MACHINE=\"${target}\"

PROG=		cc1
NOMAN=		1
NOSHARED?=	yes

SRCS=	c-parse.y c-lang.c stub-objc.c

SRCS+=	attribs.c c-errors.c c-lex.c c-pragma.c c-decl.c c-typeck.c \
	c-convert.c c-aux-info.c c-common.c c-opts.c c-format.c c-semantics.c \
	c-incpath.c cppdefault.c c-ppoutput.c c-cppbuiltin.c prefix.c \
	c-objc-common.c c-dump.c c-pch.c \
	c-gimplify.c tree-mudflap.c c-pretty-print.c

SRCS+=	main.c

SRCS+=	tm-preds.h tree-check.h genrtl.h insn-flags.h insn-constants.h
SRCS+=	gtype-desc.h insn-modes.h insn-config.h options.h insn-modes.c
SRCS+=	insn-emit.c insn-codes.h insn-attrtab.c insn-attr.h insn-extract.c
SRCS+=	insn-opinit.c genrtl.c

LDADD+=	-L../libbackend -lbackend -L../libcpp -lcpp

CFLAGS+= -I.

.include <bsd.prog.mk>
