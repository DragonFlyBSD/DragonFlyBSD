# $DragonFly: src/gnu/usr.bin/cc34/f771/Attic/Makefile,v 1.2 2004/06/21 03:48:06 dillon Exp $

GCC_LANG_DIR=	gcc/f

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

CFLAGS+=	-DTARGET_MACHINE=\"${target}\" -DHOST_MACHINE=\"${target}\"

PROG=		f771
NOMAN=		1
NOSHARED?=	yes

SRCS=	bad.c bit.c bld.c com.c data.c equiv.c expr.c global.c implic.c info.c
SRCS+=	intrin.c lab.c lex.c malloc.c name.c parse.c src.c st.c sta.c stb.c
SRCS+=	stc.c std.c ste.c storag.c stp.c str.c sts.c stt.c stu.c stv.c stw.c
SRCS+=	symbol.c target.c top.c type.c where.c main.c
SRCS+=	tree-check.h gtype-desc.h insn-modes.h insn-constants.h insn-flags.h
SRCS+=	insn-config.h insn-attr.h genrtl.h insn-codes.h tm-preds.h options.h
SRCS+=	insn-modes.c insn-emit.c insn-attrtab.c insn-opinit.c insn-extract.c

SRCS+=	str-1t.h str-2t.h str-fo.h str-io.h str-nq.h str-op.h str-ot.h

SRCS+=	concat.c fibheap.c getpwd.c getruntime.c hashtab.c hex.c lbasename.c
SRCS+=	lrealpath.c md5.c obstack.c physmem.c
SRCS+=	safe-ctype.c splay-tree.c xexit.c xmalloc.c xstrdup.c xstrerror.c

str-1t.j str-1t.h: ${TOOLDIR}/fini/fini.nx
	${TOOLDIR}/fini/fini.nx ${GCCDIR}/gcc/f/str-1t.fin str-1t.j str-1t.h

.ORDER: str-1t.j str-1t.h

str-2t.j str-2t.h: ${TOOLDIR}/fini/fini.nx
	${TOOLDIR}/fini/fini.nx ${GCCDIR}/gcc/f/str-2t.fin str-2t.j str-2t.h

.ORDER: str-2t.j str-2t.h

str-fo.j str-fo.h: ${TOOLDIR}/fini/fini.nx
	${TOOLDIR}/fini/fini.nx ${GCCDIR}/gcc/f/str-fo.fin str-fo.j str-fo.h

.ORDER: str-fo.j str-fo.h

str-io.j str-io.h: ${TOOLDIR}/fini/fini.nx
	${TOOLDIR}/fini/fini.nx ${GCCDIR}/gcc/f/str-io.fin str-io.j str-io.h

.ORDER: str-io.j str-io.h

str-nq.j str-nq.h: ${TOOLDIR}/fini/fini.nx
	${TOOLDIR}/fini/fini.nx ${GCCDIR}/gcc/f/str-nq.fin str-nq.j str-nq.h

.ORDER: str-nq.j str-nq.h

str-op.j str-op.h: ${TOOLDIR}/fini/fini.nx
	${TOOLDIR}/fini/fini.nx ${GCCDIR}/gcc/f/str-op.fin str-op.j str-op.h

.ORDER: str-op.j str-op.h

str-ot.j str-ot.h: ${TOOLDIR}/fini/fini.nx
	${TOOLDIR}/fini/fini.nx ${GCCDIR}/gcc/f/str-ot.fin str-ot.j str-ot.h

.ORDER: str-ot.j str-ot.h

CLEANFILES+=	str-1t.j str-2t.j str-fo.j str-io.j str-nq.j str-op.j str-ot.j
CLEANFILES+=	str-1t.h str-2t.h str-fo.h str-io.h str-nq.h str-op.h str-ot.h

LDADD=	-L../libbackend -lbackend -L../libcpp -lcpp

CFLAGS+=	-I.

.include <bsd.prog.mk>

depend: str-1t.j str-2t.j str-fo.j str-io.j str-nq.j str-op.j str-ot.j
