# $DragonFly: src/gnu/usr.bin/cc34/libbackend/Makefile,v 1.1 2004/06/14 22:27:56 joerg Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

CFLAGS+=	-I${.CURDIR}/../config -I.
CFLAGS+=	-DTARGET_NAME=\"${target}\"

.PATH: ${GCCDIR}/gcc/config/${GCC_CPU}

LIB=		backend
INTERNALLIB=	YES

SRCS+=	alias.c bb-reorder.c bitmap.c builtins.c caller-save.c calls.c
SRCS+=	cfg.c cfganal.c cfgbuild.c cfgcleanup.c cfglayout.c cfgloop.c
SRCS+=	cfgloopanal.c cfgloopmanip.c loop-init.c loop-unswitch.c loop-unroll.c
SRCS+=	cfgrtl.c combine.c conflict.c convert.c coverage.c cse.c cselib.c
SRCS+=	dbxout.c debug.c df.c diagnostic.c dojump.c doloop.c dominance.c
SRCS+=	dwarf2asm.c dwarf2out.c emit-rtl.c except.c explow.c expmed.c expr.c
SRCS+=	final.c flow.c fold-const.c function.c gcse.c genrtl.c ggc-common.c
SRCS+=	global.c graph.c gtype-desc.c haifa-sched.c hooks.c ifcvt.c
SRCS+=	insn-output.c insn-peep.c insn-recog.c integrate.c intl.c jump.c
SRCS+=	langhooks.c lcm.c lists.c local-alloc.c loop.c optabs.c options.c
SRCS+=	opts.c params.c postreload.c predict.c print-rtl.c print-tree.c
SRCS+=	value-prof.c profile.c ra.c ra-build.c ra-colorize.c ra-debug.c
SRCS+=	ra-rewrite.c real.c recog.c reg-stack.c regclass.c regmove.c
SRCS+=	regrename.c reload.c reload1.c reorg.c resource.c rtl.c rtlanal.c
SRCS+=	rtl-error.c sbitmap.c sched-deps.c sched-ebb.c sched-rgn.c sched-vis.c
SRCS+=	sdbout.c sibcall.c simplify-rtx.c sreal.c stmt.c stor-layout.c
SRCS+=	stringpool.c targhooks.c timevar.c toplev.c tracer.c tree.c
SRCS+=	tree-dump.c unroll.c varasm.c varray.c version.c vmsdbgout.c
SRCS+=	xcoffout.c alloc-pool.c et-forest.c cfghooks.c bt-load.c
SRCS+=	pretty-print.c ggc-page.c web.c host-default.c hashtable.c
SRCS+=	tree-inline.c tree-optimize.c cgraph.c cgraphunit.c

SRCS+=	i386.c

SRCS+=	insn-config.h genrtl.h insn-codes.h tm-preds.h insn-modes.h
SRCS+=	insn-constants.h insn-flags.h tree-check.h gtype-desc.h
SRCS+=	insn-attr.h gcov-iov.h

.include <bsd.lib.mk>
