# $DragonFly: src/gnu/usr.bin/cc34/cc1plus/Makefile,v 1.1 2004/06/14 22:27:53 joerg Exp $

GCC_LANG_DIR=	gcc/cp

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

CFLAGS+=	-DTARGET_MACHINE=\"${target}\" -DHOST_MACHINE=\"${target}\"

PROG=	cc1plus
NOMAN=	1

SRCS+=	call.c decl.c expr.c pt.c typeck2.c class.c
SRCS+=	decl2.c error.c lex.c parser.c ptree.c rtti.c
SRCS+=	typeck.c cvt.c except.c friend.c init.c method.c
SRCS+=	search.c semantics.c tree.c repo.c dump.c optimize.c
SRCS+=	mangle.c cp-lang.c name-lookup.c cxx-pretty-print.c

SRCS+=	attribs.c c-common.c c-format.c c-pragma.c c-semantics.c c-lex.c
SRCS+=	c-dump.c  c-pretty-print.c c-opts.c c-pch.c c-incpath.c cppdefault.c
SRCS+=	c-ppoutput.c c-cppbuiltin.c prefix.c main.c  

SRCS+=	tm-preds.h tree-check.h genrtl.h insn-flags.h insn-constants.h
SRCS+=	gtype-desc.h insn-modes.h insn-config.h options.h insn-modes.c
SRCS+=	insn-emit.c insn-codes.h insn-attrtab.c insn-attr.h insn-extract.c
SRCS+=	insn-opinit.c

SRCS+=	concat.c dyn-string.c fibheap.c getpwd.c getruntime.c hashtab.c hex.c
SRCS+=	lbasename.c lrealpath.c md5.c obstack.c physmem.c safe-ctype.c
SRCS+=	splay-tree.c xexit.c xmalloc.c xmemdup.c xstrdup.c xstrerror.c

LDADD=	-L../libbackend -lbackend -L../libcpp -lcpp

CFLAGS+= -I.

.include <bsd.prog.mk>
