# $DragonFly: src/gnu/usr.bin/cc41/libbackend/Makefile,v 1.1 2006/09/27 12:10:35 corecode Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

CFLAGS+=	-I${.CURDIR}/../config -I.
CFLAGS+=	-DTARGET_NAME=\"${target_machine}\"

.PATH: ${GCCDIR}/gcc/config/${GCC_CPU}

LIB=		backend
INTERNALLIB=	YES

SRCS= \
 tree-chrec.c tree-scalar-evolution.c tree-data-ref.c			   \
 tree-cfg.c tree-dfa.c tree-eh.c tree-ssa.c tree-optimize.c tree-gimple.c  \
 gimplify.c tree-pretty-print.c tree-into-ssa.c				   \
 tree-outof-ssa.c tree-ssa-ccp.c tree-vn.c tree-ssa-uncprop.c		   \
 tree-ssa-dce.c  tree-ssa-copy.c tree-nrv.c tree-ssa-copyrename.c	   \
 tree-ssa-pre.c tree-ssa-live.c tree-ssa-operands.c tree-ssa-alias.c       \
 tree-ssa-phiopt.c tree-ssa-forwprop.c tree-nested.c tree-ssa-dse.c	   \
 tree-ssa-dom.c domwalk.c tree-tailcall.c gimple-low.c tree-iterator.c	   \
 tree-phinodes.c tree-ssanames.c tree-sra.c tree-complex.c		   \
 tree-vect-generic.c tree-ssa-loop.c tree-ssa-loop-niter.c		   \
 tree-ssa-loop-manip.c tree-ssa-threadupdate.c				   \
 tree-vectorizer.c tree-vect-analyze.c tree-vect-transform.c		   \
 tree-ssa-loop-ivcanon.c tree-ssa-propagate.c tree-ssa-address.c	   \
 tree-ssa-math-opts.c							   \
 tree-ssa-loop-ivopts.c tree-if-conv.c tree-ssa-loop-unswitch.c		   \
 alias.c bb-reorder.c bitmap.c builtins.c caller-save.c calls.c	  	   \
 cfg.c cfganal.c cfgbuild.c cfgcleanup.c cfglayout.c cfgloop.c		   \
 cfgloopanal.c cfgloopmanip.c loop-init.c loop-unswitch.c loop-unroll.c	   \
 cfgrtl.c combine.c conflict.c convert.c coverage.c cse.c cselib.c 	   \
 dbxout.c ddg.c tree-ssa-loop-ch.c loop-invariant.c tree-ssa-loop-im.c	   \
 debug.c df.c diagnostic.c dojump.c dominance.c loop-doloop.c		   \
 dwarf2asm.c dwarf2out.c emit-rtl.c except.c explow.c loop-iv.c		   \
 expmed.c expr.c final.c flow.c fold-const.c function.c gcse.c		   \
 genrtl.c ggc-common.c global.c graph.c gtype-desc.c			   \
 haifa-sched.c hooks.c ifcvt.c insn-attrtab.c insn-emit.c insn-modes.c	   \
 insn-extract.c insn-opinit.c insn-output.c insn-peep.c insn-recog.c	   \
 integrate.c intl.c jump.c  langhooks.c lcm.c lists.c local-alloc.c  	   \
 loop.c mode-switching.c modulo-sched.c optabs.c options.c opts.c	   \
 params.c postreload.c postreload-gcse.c predict.c			   \
 insn-preds.c pointer-set.c 					   	   \
 print-rtl.c print-tree.c profile.c value-prof.c var-tracking.c		   \
 real.c recog.c reg-stack.c regclass.c regmove.c regrename.c		   \
 reload.c reload1.c reorg.c resource.c rtl.c rtlanal.c rtl-error.c	   \
 sbitmap.c sched-deps.c sched-ebb.c sched-rgn.c sched-vis.c sdbout.c	   \
 simplify-rtx.c sreal.c stmt.c stor-layout.c stringpool.c		   \
 targhooks.c timevar.c toplev.c tracer.c tree.c tree-dump.c		   \
 varasm.c varray.c vec.c version.c vmsdbgout.c xcoffout.c alloc-pool.c	   \
 et-forest.c cfghooks.c bt-load.c pretty-print.c ggc-page.c web.c passes.c	   \
 tree-profile.c rtlhooks.c cfgexpand.c lambda-mat.c    			   \
 lambda-trans.c	lambda-code.c tree-loop-linear.c tree-ssa-sink.c 	   \
 tree-vrp.c tree-stdarg.c tree-cfgcleanup.c tree-ssa-reassoc.c		   \
 tree-ssa-structalias.c tree-object-size.c

SRCS+=	i386.c

SRCS+=	\
  host-default.c tree-inline.c		   \
  cgraph.c cgraphunit.c tree-nomudflap.c ipa.c ipa-inline.c                \
  ipa-utils.c ipa-reference.c ipa-pure-const.c ipa-type-escape.c           \
  ipa-prop.c ipa-cp.c

SRCS+=	insn-config.h genrtl.h insn-codes.h tm-preds.h insn-modes.h
SRCS+=	insn-constants.h insn-flags.h tree-check.h gtype-desc.h
SRCS+=	insn-attr.h gcov-iov.h options.h


.include <bsd.lib.mk>
