# $DragonFly: src/gnu/lib/gcc41/libstdc++/Makefile,v 1.4 2007/08/23 06:13:30 corecode Exp $

.include "../Makefile.inc"

.if ${MACHINE_ARCH} == "i386" && !empty(MACHINE_CPU:Mi486)
MARCHDIR=	i486
.elif ${MACHINE_ARCH} == "x86_64"
MARCHDIR=	i486
.else
MARCHDIR=	${MACHINE_ARCH}
.endif

SRCDIR=	${GCCDIR}/libstdc++-v3
SUPDIR=	${SRCDIR}/libsupc++

.PATH:	${SRCDIR}/src ${SRCDIR}/libmath ${SRCDIR}/config/io
.PATH:	${SRCDIR}/config/locale/generic ${SRCDIR}
.PATH:	${SUPDIR} ${GCCDIR}/gcc ${GCCDIR}/libiberty

#SUBDIR=	doc

LIB=		stdc++
SHLIB_MAJOR=	9

CFLAGS+=	-DIN_GLIBCPP_V3 -DHAVE_CONFIG_H
CFLAGS+=	-I. -I${.CURDIR}
CFLAGS+=	-I${SUPDIR} -I${GCCDIR}/gcc -I${GCCDIR}/include
CXXFLAGS+=	-I${SRCDIR}/config/cpu/${MARCHDIR}
CXXFLAGS+=	-fno-implicit-templates -ffunction-sections -fdata-sections \
		-Wno-deprecated

LDADD+=		-lm
DPADD+=		${LIBM}

# libstdc++ sources (libstdc++/src/Makefile.in, do not reorder)
SRCS+=	\
	bitmap_allocator.cc \
	pool_allocator.cc \
	mt_allocator.cc \
	codecvt.cc \
	compatibility.cc \
	complex_io.cc \
	ctype.cc \
	debug.cc \
	debug_list.cc \
	functexcept.cc \
	globals_locale.cc \
	globals_io.cc \
	ios.cc \
	ios_failure.cc \
	ios_init.cc \
	ios_locale.cc \
	limits.cc \
	list.cc \
	locale.cc \
	locale_init.cc \
	locale_facets.cc \
	localename.cc \
	stdexcept.cc \
	strstream.cc \
	tree.cc \
	allocator-inst.cc \
	concept-inst.cc \
	fstream-inst.cc \
	ext-inst.cc \
	ios-inst.cc \
	iostream-inst.cc \
	istream-inst.cc \
	istream.cc \
	locale-inst.cc \
	locale-misc-inst.cc \
	misc-inst.cc \
	ostream-inst.cc \
	sstream-inst.cc \
	streambuf-inst.cc \
	streambuf.cc \
	string-inst.cc \
	valarray-inst.cc \
	wlocale-inst.cc \
	wstring-inst.cc \
	${host_sources} \
	${host_sources_extra}

host_sources = \
	atomicity.cc \
	codecvt_members.cc \
	collate_members.cc \
	ctype_members.cc \
	messages_members.cc \
	monetary_members.cc \
	numeric_members.cc \
	time_members.cc 

host_sources_extra = \
	basic_file_stdio.cc \
	c_locale.cc

SRCS+=	ctype_noninline.h ctype_base.h

atomicity.cc:
	echo '#include "atomicity.h"' > ${.TARGET}

CLEANFILES+=	atomicity.cc

# C parts of math
SRCS+=	stubs.c

# Embedded copy of libsupc++
SRCS+=	\
	del_op.cc \
	del_opnt.cc \
	del_opv.cc \
	del_opvnt.cc \
	eh_alloc.cc \
	eh_arm.cc \
	eh_aux_runtime.cc \
	eh_call.cc \
	eh_catch.cc \
	eh_exception.cc \
	eh_globals.cc \
	eh_personality.cc \
	eh_term_handler.cc \
	eh_terminate.cc \
	eh_throw.cc \
	eh_type.cc \
	eh_unex_handler.cc \
	guard.cc \
	new_handler.cc \
	new_op.cc \
	new_opnt.cc \
	new_opv.cc \
	new_opvnt.cc \
	pure.cc \
	tinfo.cc \
	tinfo2.cc \
	vec.cc \
	vterminate.cc

# from libiberty
SRCS+=	cp-demangle.c

SRCS+=	unwind.h

unwind.h: unwind-generic.h
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	unwind.h


strstream.cc:	${SRCDIR}/src/strstream.cc
	sed -e 's,^#include <strstream>,#include <backward/strstream>,g' \
	    < ${.ALLSRC} > ${.TARGET}

CLEANFILES+=	strstream.cc


INCSGROUPS=	BITSHDRS BKWHDRS DEBUGHDRS EXTHDRS TR1HDRS \
		BASEHDRS BASEXHDRS STDHDRS TARGETHDRS THRHDRS

STDHDRS=	\
	std_algorithm.h \
	std_bitset.h \
	std_complex.h \
	std_deque.h \
	std_fstream.h \
	std_functional.h \
	std_iomanip.h \
	std_ios.h \
	std_iosfwd.h \
	std_iostream.h \
	std_istream.h \
	std_iterator.h \
	std_limits.h \
	std_list.h \
	std_locale.h \
	std_map.h \
	std_memory.h \
	std_numeric.h \
	std_ostream.h \
	std_queue.h \
	std_set.h \
	std_sstream.h \
	std_stack.h \
	std_stdexcept.h \
	std_streambuf.h \
	std_string.h \
	std_utility.h \
	std_valarray.h \
	std_vector.h
.for h in ${STDHDRS}
STDHDRSNAME_$h=${h:R:S;^std_;;}
.endfor
STDHDRS:=	${STDHDRS:S;^;${SRCDIR}/include/std/;}
STDHDRSDIR=	${INCLUDEDIR}/c++/4.1


BITSHDRS=	\
	allocator.h \
	atomicity.h \
	basic_ios.h \
	basic_ios.tcc \
	basic_string.h \
	basic_string.tcc \
	boost_concept_check.h \
	char_traits.h \
	codecvt.h \
	concept_check.h \
	concurrence.h \
	cpp_type_traits.h \
	deque.tcc \
	fstream.tcc \
	functexcept.h \
	gslice.h \
	gslice_array.h \
	indirect_array.h \
	ios_base.h \
	istream.tcc \
	list.tcc \
	locale_classes.h \
	locale_facets.h \
	locale_facets.tcc \
	localefwd.h \
	mask_array.h \
	ostream.tcc \
	postypes.h \
	stream_iterator.h \
	streambuf_iterator.h \
	slice_array.h \
	sstream.tcc \
	stl_algo.h \
	stl_algobase.h \
	stl_bvector.h \
	stl_construct.h \
	stl_deque.h \
	stl_function.h \
	stl_heap.h \
	stl_iterator.h \
	stl_iterator_base_funcs.h \
	stl_iterator_base_types.h \
	stl_list.h \
	stl_map.h \
	stl_multimap.h \
	stl_multiset.h \
	stl_numeric.h \
	stl_pair.h \
	stl_queue.h \
	stl_raw_storage_iter.h \
	stl_relops.h \
	stl_set.h \
	stl_stack.h \
	stl_tempbuf.h \
	stl_tree.h \
	stl_uninitialized.h \
	stl_vector.h \
	streambuf.tcc \
	stringfwd.h \
	valarray_array.h \
	valarray_array.tcc \
	valarray_before.h \
	valarray_after.h \
	vector.tcc
BITSHDRS:=	${BITSHDRS:S;^;${SRCDIR}/include/bits/;}
BITSHDRSDIR=	${INCLUDEDIR}/c++/4.1/bits


BKWHDRS=	\
	complex.h \
	iomanip.h \
	istream.h \
	ostream.h \
	stream.h \
	streambuf.h \
	algo.h \
	algobase.h \
	alloc.h \
	bvector.h \
	defalloc.h \
	deque.h \
	function.h \
	hash_map.h \
	hash_set.h \
	hashtable.h \
	heap.h \
	iostream.h \
	iterator.h \
	list.h \
	map.h \
	multimap.h \
	new.h \
	multiset.h \
	pair.h \
	queue.h \
	rope.h \
	set.h \
	slist.h \
	stack.h \
	tempbuf.h \
	tree.h \
	vector.h \
	fstream.h \
	strstream \
	backward_warning.h
BKWHDRS:=	${BKWHDRS:S;^;${SRCDIR}/include/backward/;}
BKWHDRSDIR=	${INCLUDEDIR}/c++/4.1/backward


EXTHDRS=	\
	algorithm \
	array_allocator.h \
	bitmap_allocator.h \
	codecvt_specializations.h \
	debug_allocator.h \
	stdio_filebuf.h \
	stdio_sync_filebuf.h \
	functional \
	hash_map \
	hash_set \
	hash_fun.h \
	hashtable.h \
	iterator \
	malloc_allocator.h \
	memory \
	mt_allocator.h \
	new_allocator.h \
	numeric \
	pod_char_traits.h \
	pool_allocator.h \
	rb_tree \
	rope \
	ropeimpl.h \
	slist \
	typelist.h \
	rc_string_base.h \
	sso_string_base.h \
	vstring.h \
	vstring.tcc \
	vstring_fwd.h \
	vstring_util.h	
EXTHDRS:=	${EXTHDRS:S;^;${SRCDIR}/include/ext/;}
EXTHDRSDIR=	${INCLUDEDIR}/c++/4.1/ext


TR1HDRS=	\
	array \
	bind_repeat.h \
	bind_iterate.h \
	boost_shared_ptr.h \
	mu_iterate.h \
	functional \
	functional_iterate.h \
	memory \
	ref_fwd.h \
	ref_wrap_iterate.h \
	repeat.h \
	tuple \
	tuple_iterate.h \
	utility \
	type_traits \
	type_traits_fwd.h \
	hashtable \
	unordered_set \
	unordered_map
TR1HDRS:=	${TR1HDRS:S;^;${SRCDIR}/include/tr1/;}
TR1HDRSDIR=	${INCLUDEDIR}/c++/4.1/tr1


# This is the common subset of files that all three "C" header models use.
BASEHDRS=	\
	std_cassert.h \
	std_cctype.h \
	std_cerrno.h \
	std_cfloat.h \
	std_ciso646.h \
	std_climits.h \
	std_clocale.h \
	std_cmath.h \
	std_csetjmp.h \
	std_csignal.h \
	std_cstdarg.h \
	std_cstddef.h \
	std_cstdio.h \
	std_cstdlib.h \
	std_cstring.h \
	std_ctime.h \
	std_cwchar.h \
	std_cwctype.h
.for h in ${BASEHDRS}
BASEHDRSNAME_$h=${h:R:S;^std_;;}
.endfor
BASEHDRS:=	${BASEHDRS:S;^;${SRCDIR}/include/c_std/;}
BASEHDRSDIR=	${INCLUDEDIR}/c++/4.1

# Some of the different "C" header models need extra files.
BASEXHDRS=	${SRCDIR}/include/c_std/cmath.tcc
BASEXHDRSDIR=	${INCLUDEDIR}/c++/4.1/bits


DEBUGHDRS=	\
	bitset \
	debug.h \
	deque \
	formatter.h \
	functions.h \
	hash_map \
	hash_map.h \
	hash_multimap.h \
	hash_multiset.h \
	hash_set \
	hash_set.h \
	list \
	map \
	macros.h \
	map.h \
	multimap.h \
	multiset.h \
	safe_base.h \
	safe_iterator.h \
	safe_iterator.tcc \
	safe_sequence.h \
	set \
	set.h \
	string \
	vector
DEBUGHDRS:=	${DEBUGHDRS:S;^;${SRCDIR}/include/debug/;}
DEBUGHDRSDIR=	${INCLUDEDIR}/c++/4.1/debug


# c++locale <- c_locale.h
TARGETHDRS=	io/basic_file_stdio.h
TARGETHDRSNAME_basic_file_stdio.h=	basic_file.h
TARGETHDRS+=	allocator/new_allocator_base.h
TARGETHDRSNAME_new_allocator_base.h=	c++allocator.h
TARGETHDRS+=	io/c_io_stdio.h
TARGETHDRSNAME_c_io_stdio.h=		c++io.h
TARGETHDRS+=	locale/generic/c_locale.h locale/generic/c++locale_internal.h
TARGETHDRSNAME_c_locale.h=		c++locale.h
TARGETHDRS+=	locale/generic/messages_members.h
TARGETHDRS+=	locale/generic/time_members.h
TARGETHDRS+=	os/bsd/netbsd/ctype_inline.h
TARGETHDRS:=	${TARGETHDRS:S;^;${SRCDIR}/config/;}
TARGETHDRS+=	ctype_base.h ctype_noninline.h os_defines.h
TARGETHDRS+=	c++config.h
TARGETHDRSDIR=	${INCLUDEDIR}/c++/4.1/bits

MARCHHDRS=	atomic_word.h cxxabi_tweaks.h cpu_defines.h

.for h in ${MARCHHDRS}
.if exists(${SRCDIR}/config/cpu/${MARCHDIR}/${h})
TARGETHDRS+=	${SRCDIR}/config/cpu/${MARCHDIR}/${h}
.else
TARGETHDRS+=	${SRCDIR}/config/cpu/generic/${h}
.endif
.endfor

THRHDRS=	gthr.h gthr-single.h gthr-posix.h gthr-default.h
THRHDRSDIR=	${INCLUDEDIR}/c++/4.1/bits

gthr.h: ${GCCDIR}/gcc/gthr.h
	sed -e '/^#pragma/b' \
	    -e '/^#/s/\([ABCDEFGHIJKLMNOPQRSTUVWXYZ_][ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*\)/_GLIBCXX_\1/g' \
	    -e 's/_GLIBCXX_SUPPORTS_WEAK/__GXX_WEAK__/g' \
	    -e 's,^#include "\(.*\)",#include <bits/\1>,g' \
	    < ${GCCDIR}/gcc/gthr.h > gthr.h

gthr-single.h: ${GCCDIR}/gcc/gthr-single.h
	sed -e 's/\(UNUSED\)/_GLIBCXX_\1/g' \
	    -e 's/\(GCC[ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*_H\)/_GLIBCXX_\1/g' \
	    < ${GCCDIR}/gcc/gthr-single.h > gthr-single.h

gthr-posix.h: ${GCCDIR}/gcc/gthr-posix.h
	sed -e 's/\(UNUSED\)/_GLIBCXX_\1/g' \
	    -e 's/\(GCC[ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*_H\)/_GLIBCXX_\1/g' \
	    -e 's/SUPPORTS_WEAK/__GXX_WEAK__/g' \
	    -e 's/\([ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*USE_WEAK\)/_GLIBCXX_\1/g' \
	    < ${GCCDIR}/gcc/gthr-posix.h > gthr-posix.h

gthr-default.h: ${GCCDIR}/gcc/gthr-posix.h
	sed -e 's/\(UNUSED\)/_GLIBCXX_\1/g' \
	    -e 's/\(GCC[ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*_H\)/_GLIBCXX_\1/g' \
	    -e 's/SUPPORTS_WEAK/__GXX_WEAK__/g' \
	    -e 's/\([ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*USE_WEAK\)/_GLIBCXX_\1/g' \
	    -e 's,^#include "\(.*\)",#include <bits/\1>,g' \
	    < ${GCCDIR}/gcc/gthr-posix.h > gthr-default.h

ctype_base.h: ${SRCDIR}/config/os/bsd/netbsd/ctype_base.h ctype_base.h.patch
	patch -o ${.TARGET} -i ${.CURDIR}/ctype_base.h.patch ${SRCDIR}/config/os/bsd/netbsd/ctype_base.h

ctype_noninline.h: ${SRCDIR}/config/os/bsd/netbsd/ctype_noninline.h ctype_noninline.h.patch
	patch -o ${.TARGET} -i ${.CURDIR}/ctype_noninline.h.patch ${SRCDIR}/config/os/bsd/netbsd/ctype_noninline.h

c++config.h: config.h ${SRCDIR}/include/bits/c++config DATESTAMP
	cat ${.ALLSRC:M*c++config} > ${.TARGET}
	sed -e 's/^/#define __GLIBCXX__ /' < ${.ALLSRC:M*DATESTAMP} >> ${.TARGET}
	sed -e 's/HAVE_/_GLIBCXX_HAVE_/g' \
	    -e 's/PACKAGE/_GLIBCXX_PACKAGE/g' \
	    -e 's/VERSION/_GLIBCXX_VERSION/g' \
	    -e 's/WORDS_/_GLIBCXX_WORDS_/g' \
	    < ${.ALLSRC:M*config.h} >> ${.TARGET}
	echo "#endif // _CXXCONFIG_" >> ${.TARGET}

CLEANFILES+=	${THRHDRS} ctype_noninline.h ctype_base.h c++config.h

.include <bsd.lib.mk>
