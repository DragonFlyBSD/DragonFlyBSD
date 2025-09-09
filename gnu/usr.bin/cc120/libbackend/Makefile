GCC_NO_LIBS=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH: $(srcdir)/config/i386

CFLAGS+=	-DTARGET_NAME=\"${target_machine}\" -DTARGET_MACHINE=\"${target_machine}\"
# needed to compile hsa/brig bits
CXXFLAGS+=	${NOEXCEPTION_FLAGS}

LIB=		backend
INTERNALLIB=	YES

# almost verbatim from Makefile, objects from cc files have been pulled out
# because the .o=.c is wrong for them
host_hook_obj=host-default.o
out_object_file=i386.o
EXTRA_OBJS=	x86-tune-sched.o x86-tune-sched-bd.o \
		x86-tune-sched-atom.o x86-tune-sched-core.o \
		i386-options.o i386-builtins.o i386-expand.o i386-features.o
BACKEND_OBJS= \
	gimple-match.o \
	generic-match.o \
	insn-attrtab.o \
	insn-automata.o \
	insn-dfatab.o \
	insn-emit.o \
	insn-extract.o \
	insn-latencytab.o \
	insn-modes.o \
	insn-opinit.o \
	insn-output.o \
	insn-peep.o \
	insn-preds.o \
	insn-recog.o \
	insn-enums.o \
	ggc-page.o \
	adjust-alignment.o \
	alias.o \
	alloc-pool.o \
	auto-inc-dec.o \
	auto-profile.o \
	bb-reorder.o \
	bitmap.o \
	builtins.o \
	caller-save.o \
	calls.o \
	ccmp.o \
	cfg.o \
	cfganal.o \
	cfgbuild.o \
	cfgcleanup.o \
	cfgexpand.o \
	cfghooks.o \
	cfgloop.o \
	cfgloopanal.o \
	cfgloopmanip.o \
	cfgrtl.o \
	ctfc.o \
	ctfout.o \
	btfout.o \
	symtab.o \
	symtab-thunks.o \
	symtab-clones.o \
	cgraph.o \
	cgraphbuild.o \
	cgraphunit.o \
	cgraphclones.o \
	combine.o \
	combine-stack-adj.o \
	compare-elim.o \
	context.o \
	convert.o \
	coroutine-passes.o \
	coverage.o \
	cppbuiltin.o \
	cppdefault.o \
	cprop.o \
	cse.o \
	cselib.o \
	data-streamer.o \
	data-streamer-in.o \
	data-streamer-out.o \
	dbxout.o \
	dbgcnt.o \
	dce.o \
	ddg.o \
	debug.o \
	df-core.o \
	df-problems.o \
	df-scan.o \
	dfp.o \
	digraph.o \
	dojump.o \
	dominance.o \
	domwalk.o \
	double-int.o \
	dse.o \
	dumpfile.o \
	dwarf2asm.o \
	dwarf2cfi.o \
	dwarf2ctf.o \
	dwarf2out.o \
	early-remat.o \
	emit-rtl.o \
	et-forest.o \
	except.o \
	explow.o \
	expmed.o \
	expr.o \
	file-prefix-map.o \
	final.o \
	fixed-value.o \
	fold-const.o \
	fold-const-call.o \
	function.o \
	function-abi.o \
	fwprop.o \
	gcc-rich-location.o \
	gcse.o \
	gcse-common.o \
	ggc-common.o \
	ggc-tests.o \
	gimple.o \
	gimple-array-bounds.o \
	gimple-builder.o \
	gimple-expr.o \
	gimple-if-to-switch.o \
	gimple-iterator.o \
	gimple-fold.o \
	gimple-harden-conditionals.o \
	gimple-laddress.o \
	gimple-loop-interchange.o \
	gimple-loop-jam.o \
	gimple-loop-versioning.o \
	gimple-low.o \
	gimple-predicate-analysis.o \
	gimple-pretty-print.o \
	gimple-range.o \
	gimple-range-cache.o \
	gimple-range-edge.o \
	gimple-range-fold.o \
	gimple-range-gori.o \
	gimple-range-trace.o \
	gimple-ssa-backprop.o \
	gimple-ssa-evrp.o \
	gimple-ssa-evrp-analyze.o \
	gimple-ssa-isolate-paths.o \
	gimple-ssa-nonnull-compare.o \
	gimple-ssa-split-paths.o \
	gimple-ssa-store-merging.o \
	gimple-ssa-strength-reduction.o \
	gimple-ssa-sprintf.o \
	gimple-ssa-warn-access.o \
	gimple-ssa-warn-alloca.o \
	gimple-ssa-warn-restrict.o \
	gimple-streamer-in.o \
	gimple-streamer-out.o \
	gimple-walk.o \
	gimple-warn-recursion.o \
	gimplify.o \
	gimplify-me.o \
	godump.o \
	graph.o \
	graphds.o \
	graphviz.o \
	graphite.o \
	gtype-desc.o \
	haifa-sched.o \
	hw-doloop.o \
	hwint.o \
	ifcvt.o \
	ree.o \
	inchash.o \
	incpath.o \
	init-regs.o \
	internal-fn.o \
	ipa-cp.o \
	ipa-sra.o \
	ipa-devirt.o \
	ipa-fnsummary.o \
	ipa-polymorphic-call.o \
	ipa-split.o \
	ipa-inline.o \
	ipa-comdats.o \
	ipa-free-lang-data.o \
	ipa-visibility.o \
	ipa-inline-analysis.o \
	ipa-inline-transform.o \
	ipa-modref.o \
	ipa-modref-tree.o \
	ipa-predicate.o \
	ipa-profile.o \
	ipa-prop.o \
	ipa-param-manipulation.o \
	ipa-pure-const.o \
	ipa-icf.o \
	ipa-icf-gimple.o \
	ipa-reference.o \
	ipa-ref.o \
	ipa-utils.o \
	ipa.o \
	ira.o \
	ira-build.o \
	ira-costs.o \
	ira-conflicts.o \
	ira-color.o \
	ira-emit.o \
	ira-lives.o \
	jump.o \
	langhooks.o \
	lcm.o \
	lists.o \
	loop-doloop.o \
	loop-init.o \
	loop-invariant.o \
	loop-iv.o \
	loop-unroll.o \
	lower-subreg.o \
	lra.o \
	lra-assigns.o \
	lra-coalesce.o \
	lra-constraints.o \
	lra-eliminations.o \
	lra-lives.o \
	lra-remat.o \
	lra-spills.o \
	lto-cgraph.o \
	lto-streamer.o \
	lto-streamer-in.o \
	lto-streamer-out.o \
	lto-section-in.o \
	lto-section-out.o \
	lto-opts.o \
	lto-compress.o \
	mcf.o \
	mode-switching.o \
	modulo-sched.o \
	multiple_target.o \
	omp-offload.o \
	omp-expand.o \
	omp-general.o \
	omp-low.o \
	omp-oacc-kernels-decompose.o \
	omp-oacc-neuter-broadcast.o \
	omp-simd-clone.o \
	opt-problem.o \
	optabs.o \
	optabs-libfuncs.o \
	optabs-query.o \
	optabs-tree.o \
	optinfo.o \
	optinfo-emit-json.o \
	options-save.o \
	opts-global.o \
	ordered-hash-map-tests.o \
	passes.o \
	plugin.o \
	pointer-query.o \
	postreload-gcse.o \
	postreload.o \
	predict.o \
	print-rtl.o \
	print-rtl-function.o \
	print-tree.o \
	profile.o \
	profile-count.o \
	range.o \
	range-op.o \
	read-md.o \
	read-rtl.o \
	read-rtl-function.o \
	real.o \
	realmpfr.o \
	recog.o \
	reg-stack.o \
	regcprop.o \
	reginfo.o \
	regrename.o \
	regstat.o \
	reload.o \
	reload1.o \
	reorg.o \
	resource.o \
	rtl-error.o \
	rtl-ssa/accesses.o \
	rtl-ssa/blocks.o \
	rtl-ssa/changes.o \
	rtl-ssa/functions.o \
	rtl-ssa/insns.o \
	rtl.o \
	rtlhash.o \
	rtlanal.o \
	rtlhooks.o \
	rtx-vector-builder.o \
	run-rtl-passes.o \
	sched-deps.o \
	sched-ebb.o \
	sched-rgn.o \
	sel-sched-ir.o \
	sel-sched-dump.o \
	sel-sched.o \
	sese.o \
	shrink-wrap.o \
	simplify-rtx.o \
	sparseset.o \
	spellcheck.o \
	spellcheck-tree.o \
	splay-tree-utils.o \
	sreal.o \
	stack-ptr-mod.o \
	statistics.o \
	stmt.o \
	stor-layout.o \
	store-motion.o \
	streamer-hooks.o \
	stringpool.o \
	substring-locations.o \
	target-globals.o \
	targhooks.o \
	timevar.o \
	toplev.o \
	tracer.o \
	trans-mem.o \
	tree-affine.o \
	asan.o \
	tsan.o \
	ubsan.o \
	sanopt.o \
	sancov.o \
	tree-call-cdce.o \
	tree-cfg.o \
	tree-cfgcleanup.o \
	tree-chrec.o \
	tree-complex.o \
	tree-data-ref.o \
	tree-dfa.o \
	tree-diagnostic.o \
	tree-diagnostic-path.o \
	tree-dump.o \
	tree-eh.o \
	tree-emutls.o \
	tree-if-conv.o \
	tree-inline.o \
	tree-into-ssa.o \
	tree-iterator.o \
	tree-loop-distribution.o \
	tree-nested.o \
	tree-nrv.o \
	tree-object-size.o \
	tree-outof-ssa.o \
	tree-parloops.o \
	tree-phinodes.o \
	tree-predcom.o \
	tree-pretty-print.o \
	tree-profile.o \
	tree-scalar-evolution.o \
	tree-sra.o \
	tree-switch-conversion.o \
	tree-ssa-address.o \
	tree-ssa-alias.o \
	tree-ssa-ccp.o \
	tree-ssa-coalesce.o \
	tree-ssa-copy.o \
	tree-ssa-dce.o \
	tree-ssa-dom.o \
	tree-ssa-dse.o \
	tree-ssa-forwprop.o \
	tree-ssa-ifcombine.o \
	tree-ssa-live.o \
	tree-ssa-loop-ch.o \
	tree-ssa-loop-im.o \
	tree-ssa-loop-ivcanon.o \
	tree-ssa-loop-ivopts.o \
	tree-ssa-loop-manip.o \
	tree-ssa-loop-niter.o \
	tree-ssa-loop-prefetch.o \
	tree-ssa-loop-split.o \
	tree-ssa-loop-unswitch.o \
	tree-ssa-loop.o \
	tree-ssa-math-opts.o \
	tree-ssa-operands.o \
	gimple-range-path.o \
	tree-ssa-phiopt.o \
	tree-ssa-phiprop.o \
	tree-ssa-pre.o \
	tree-ssa-propagate.o \
	tree-ssa-reassoc.o \
	tree-ssa-sccvn.o \
	tree-ssa-scopedtables.o \
	tree-ssa-sink.o \
	tree-ssa-strlen.o \
	tree-ssa-structalias.o \
	tree-ssa-tail-merge.o \
	tree-ssa-ter.o \
	tree-ssa-threadbackward.o \
	tree-ssa-threadedge.o \
	tree-ssa-threadupdate.o \
	tree-ssa-uncprop.o \
	tree-ssa-uninit.o \
	tree-ssa.o \
	tree-ssanames.o \
	tree-stdarg.o \
	tree-streamer.o \
	tree-streamer-in.o \
	tree-streamer-out.o \
	tree-tailcall.o \
	tree-vect-generic.o \
	gimple-isel.o \
	tree-vect-patterns.o \
	tree-vect-data-refs.o \
	tree-vect-stmts.o \
	tree-vect-loop.o \
	tree-vect-loop-manip.o \
	tree-vect-slp.o \
	tree-vect-slp-patterns.o \
	tree-vectorizer.o \
	tree-vector-builder.o \
	tree-vrp.o \
	tree.o \
	tristate.o \
	valtrack.o \
	value-pointer-equiv.o \
	value-query.o \
	value-range.o \
	value-range-equiv.o \
	value-relation.o \
	value-prof.o \
	var-tracking.o \
	varasm.o \
	varpool.o \
	vec-perm-indices.o \
	vr-values.o \
	vtable-verify.o \
	warning-control.o \
	web.o \
	wide-int.o \
	wide-int-print.o \
	$(out_object_file) \
	$(EXTRA_OBJS) \
	$(host_hook_obj)

.if ${CFLAGS:M-DTOOLCHAIN_CHECKING}
BACKEND_OBJS+=	fibonacci_heap.o function-tests.o \
		hash-map-tests.o hash-set-tests.o \
		rtl-tests.o selftest-rtl.o \
		selftest-run-tests.o typed-splay-tree.o
.endif

# only when HAVE_isl
#BACKEND_OBJS+= \
	graphite-isl-ast-to-gimple.o \
	graphite-dependences.o \
	graphite-optimize-isl.o \
	graphite-poly.o \
	graphite-scop-detection.o \
	graphite-sese-to-poly.o

# only when VMS_DEBUGGING_INFO
#BACKEND_OBJS+= \
	vmsdbgout.o

# only when XCOFF_DEBUGGING_INFO
#BACKEND_OBJS+= \
	xcoffout.o

OBJS=	${BACKEND_OBJS}

.if ${CFLAGS:M-DTOOLCHAIN_CHECKING}
SRCS+=	unique-ptr-tests.cc
.endif

# rtl-ssa
RTL_SSA_SRCS= \
	rtl-ssa/accesses.cc \
	rtl-ssa/blocks.cc \
	rtl-ssa/changes.cc \
	rtl-ssa/functions.cc \
	rtl-ssa/insns.cc

SRCS+= ${RTL_SSA_SRCS}
${RTL_SSA_SRCS}: rtl_ssa_dir

# Analzyer, called ANALYZER_OBJS in gcc
ANALYZER_SRCS=	\
	analyzer/analysis-plan.cc \
	analyzer/analyzer.cc \
	analyzer/analyzer-logging.cc \
	analyzer/analyzer-pass.cc \
	analyzer/analyzer-selftests.cc \
	analyzer/bar-chart.cc \
	analyzer/call-info.cc \
	analyzer/call-string.cc \
	analyzer/checker-path.cc \
	analyzer/complexity.cc \
	analyzer/constraint-manager.cc \
	analyzer/diagnostic-manager.cc \
	analyzer/engine.cc \
	analyzer/feasible-graph.cc \
	analyzer/function-set.cc \
	analyzer/pending-diagnostic.cc \
	analyzer/program-point.cc \
	analyzer/program-state.cc \
	analyzer/region.cc \
	analyzer/region-model.cc \
	analyzer/region-model-asm.cc \
	analyzer/region-model-impl-calls.cc \
	analyzer/region-model-manager.cc \
	analyzer/region-model-reachability.cc \
	analyzer/sm.cc \
	analyzer/sm-file.cc \
	analyzer/sm-malloc.cc \
	analyzer/sm-pattern-test.cc \
	analyzer/sm-sensitive.cc \
	analyzer/sm-signal.cc \
	analyzer/sm-taint.cc \
	analyzer/state-purge.cc \
	analyzer/store.cc \
	analyzer/supergraph.cc \
	analyzer/svalue.cc \
	analyzer/trimmed-graph.cc

SRCS+= ${ANALYZER_SRCS}

${ANALYZER_SRCS}: analyzer_dir

# for tree-ssa-structalias.c, tree-vect-stmts.c, i386.c
.if (${CCVER:Mgcc80} || ${CCVER:Mgcc1*0})
CXXFLAGS+=	-Wno-narrowing
CFLAGS+=	-Wno-narrowing
.elif ${CCVER:Mclang*}
CXXFLAGS+=	-Wno-c++11-narrowing
.endif

.for cfile in ${BACKEND_OBJS}
${cfile}: ${cfile:.o=.cc}
	${CXX} ${STATIC_CXXFLAGS} ${CXXFLAGS} -c ${.IMPSRC} -o ${.TARGET}
.endfor

rtl_ssa_dir:
	mkdir -p rtl-ssa

analyzer_dir:
	mkdir -p analyzer

dfp.o: gstdint.h

gstdint.h:
	echo "#include <stdint.h>" > ${.TARGET}

CLEANFILES=	gstdint.h

.include <bsd.lib.mk>
