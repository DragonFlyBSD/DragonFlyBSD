# $DragonFly: src/gnu/usr.bin/cc34/cc1obj/Makefile,v 1.1 2004/06/14 22:27:53 joerg Exp $

GCC_LANG_DIR=	gcc/objc

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

CFLAGS+=	-DTARGET_MACHINE=\"${target}\" -DHOST_MACHINE=\"${target}\"

PROG=		cc1obj
NOMAN=		1
NOSHARED?=	yes

SRCS+=	objc-lang.c objc-parse.y objc-act.c attribs.c c-errors.c c-lex.c
SRCS+=	c-pragma.c c-decl.c c-typeck.c c-convert.c c-aux-info.c c-common.c
SRCS+=	c-opts.c c-format.c c-semantics.c c-incpath.c cppdefault.c c-ppoutput.c
SRCS+=	c-cppbuiltin.c prefix.c c-objc-common.c c-dump.c c-pch.c main.c

SRCS+=	tm-preds.h tree-check.h genrtl.h insn-flags.h insn-constants.h
SRCS+=	gtype-desc.h insn-modes.h insn-config.h options.h insn-modes.c
SRCS+=	insn-emit.c insn-codes.h insn-attrtab.c insn-attr.h insn-extract.c
SRCS+=	insn-opinit.c

SRCS+=	concat.c fibheap.c getpwd.c getruntime.c hashtab.c hex.c lbasename.c
SRCS+=	lrealpath.c md5.c obstack.c physmem.c safe-ctype.c splay-tree.c
SRCS+=	xexit.c xmalloc.c xmemdup.c xstrdup.c xstrerror.c

LDADD=	-L../libbackend -lbackend -L../libcpp -lcpp

CFLAGS+=	-I${GCCDIR}/gcc/objc
CFLAGS+=	-I.

.include <bsd.prog.mk>
