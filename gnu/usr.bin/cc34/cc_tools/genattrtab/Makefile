# $DragonFly: src/gnu/usr.bin/cc34/cc_tools/genattrtab/Makefile,v 1.1 2004/06/14 22:27:54 joerg Exp $

.include "../Makefile.inc"

PROG=	genattrtab
SRCS=	genattrtab.c genrtl.h insn-modes.h gtype-desc.h gensupport.c
SRCS+=	insn-conditions.c tm-preds.h insn-constants.h min-insn-modes.c
SRCS+=	tree-check.h
SRCS+=	errors.c read-rtl.c rtl.c ggc-none.c genautomata.c varray.c
SRCS+=	print-rtl.c bitmap.c
SRCS+=	concat.c getruntime.c hashtab.c obstack.c safe-ctype.c xexit.c
SRCS+=	xmalloc.c xstrdup.c
LDADD=	-lm

.include <bsd.hostprog.mk>
