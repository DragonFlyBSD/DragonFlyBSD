# $DragonFly: src/gnu/lib/gcc34/libstdc++/Makefile,v 1.1 2004/06/14 22:27:52 joerg Exp $

.include "../Makefile.inc"

.if ${MACHINE_ARCH} == "sparc64"
MARCHDIR=	sparc
.elif ${MACHINE_ARCH} == "i386" && ${MACHINE_CPU} != 'i386'
MARCHDIR=	i486
.else
MARCHDIR=	${MACHINE_ARCH}
.endif

SRCDIR=	${GCCDIR}/libstdc++-v3
SUPDIR=	${SRCDIR}/libsupc++

.PATH:	${SRCDIR}/src ${SRCDIR}/libmath ${SRCDIR}/config/io
.PATH:	${SRCDIR}/config/locale/generic ${SRCDIR} ${SRCDIR}/std
.PATH:	${SUPDIR} ${GCCDIR}/gcc ${GCCDIR}/libiberty

#SUBDIR=	doc

LIB=		stdc++
SHLIB_MAJOR=	6

CFLAGS+=	-DIN_GLIBCPP_V3 -DHAVE_CONFIG_H
CFLAGS+=	-I${.CURDIR} -I${SUPDIR} -I${GCCDIR}/gcc -I${GCCDIR}/include
CXXFLAGS+=	-I${SRCDIR}/config/cpu/${MARCHDIR}
CXXFLAGS+=	-fno-implicit-templates -ffunction-sections -fdata-sections \
		-Wno-deprecated
#LDFLAGS+=	-Wl,-version-info,4:0:0 -Wl,--version-script=${SRCDIR}/src/linker.map

LDADD+=		-lm
DPADD+=		${LIBM}

# libstdc++ sources (libstdc++/src/Makefile.in, do not reorder)
SRCS+=	allocator.cc codecvt.cc complex_io.cc ctype.cc debug.cc debug_list.cc
SRCS+=	functexcept.cc globals_locale.cc globals_io.cc ios.cc ios_failure.cc
SRCS+=	ios_init.cc ios_locale.cc limits.cc list.cc locale.cc locale_init.cc
SRCS+=	locale_facets.cc localename.cc stdexcept.cc strstream.cc tree.cc
SRCS+=	allocator-inst.cc concept-inst.cc fstream-inst.cc ext-inst.cc
SRCS+=	io-inst.cc istream-inst.cc locale-inst.cc locale-misc-inst.cc
SRCS+=	misc-inst.cc ostream-inst.cc sstream-inst.cc streambuf-inst.cc
SRCS+=	string-inst.cc valarray-inst.cc wlocale-inst.cc
SRCS+=	basic_file_stdio.cc c_locale.cc

# target sources
SRCS+=	codecvt_members.cc collate_members.cc ctype_members.cc
SRCS+=	messages_members.cc monetary_members.cc numeric_members.cc
SRCS+=	time_members.cc

atomicity.cc:
	echo '#include "atomicity.h"' > ${.TARGET}

SRCS+=		atomicity.cc
CLEANFILES+=	atomicity.cc

# C parts of math
SRCS+=	signbit.c signbitf.c stubs.c

# Embedded copy of libsupc++
SRCS+=	del_op.cc del_opnt.cc del_opv.cc del_opvnt.cc
SRCS+=	eh_alloc.cc eh_aux_runtime.cc eh_catch.cc eh_exception.cc
SRCS+=	eh_globals.cc eh_personality.cc eh_term_handler.cc eh_terminate.cc
SRCS+=	eh_throw.cc eh_type.cc eh_unex_handler.cc guard.cc new_handler.cc
SRCS+=	new_op.cc new_opnt.cc new_opv.cc new_opvnt.cc pure.cc tinfo.cc
SRCS+=	tinfo2.cc vec.cc vterminate.cc

# from libiberty:
SRCS+=	cp-demangle.c
# dyn-string.c

strstream-fixed.cc:	${SRCDIR}/src/strstream.cc
	sed -e 's,^#include <strstream>,#include <backward/strstream>,g' \
	    < ${SRCDIR}/src/strstream.cc > strstream-fixed.cc

CLEANFILES+=	strstream-fixed.cc

INCSGROUPS=	BITSHDRS BKWHDRS DEBUGHDRS EXTHDRS BASEHDRS BASEXHDRS STDHDRS \
		TARGETHDRS THRHDRS

BITSHDRS=	allocator.h atomicity.h basic_ios.h basic_ios.tcc
BITSHDRS+=	basic_string.h basic_string.tcc boost_concept_check.h
BITSHDRS+=	char_traits.h codecvt.h concept_check.h
BITSHDRS+=	concurrence.h cpp_type_traits.h deque.tcc fstream.tcc
BITSHDRS+=	functexcept.h gslice.h gslice_array.h indirect_array.h
BITSHDRS+=	ios_base.h istream.tcc list.tcc locale_classes.h
BITSHDRS+=	locale_facets.h locale_facets.tcc localefwd.h mask_array.h
BITSHDRS+=	ostream.tcc postypes.h slice_array.h sstream.tcc stl_algo.h
BITSHDRS+=	stl_algobase.h stl_bvector.h stl_construct.h stl_deque.h
BITSHDRS+=	stl_function.h stl_heap.h stl_iterator.h
BITSHDRS+=	stl_iterator_base_funcs.h stl_iterator_base_types.h stl_list.h
BITSHDRS+=	stl_map.h stl_multimap.h stl_multiset.h stl_numeric.h
BITSHDRS+=	stl_pair.h stl_queue.h stl_raw_storage_iter.h stl_relops.h
BITSHDRS+=	stl_set.h stl_stack.h stl_tempbuf.h stl_threads.h stl_tree.h
BITSHDRS+=	stl_uninitialized.h stl_vector.h stream_iterator.h
BITSHDRS+=	streambuf.tcc streambuf_iterator.h stringfwd.h type_traits.h
BITSHDRS+=	valarray_after.h valarray_array.h valarray_array.tcc
BITSHDRS+=	valarray_before.h vector.tcc

BITSHDRS:=	${BITSHDRS:S;^;${SRCDIR}/include/bits/;}
BITSHDRSDIR=	${INCLUDEDIR}/c++/3.4/bits

BKWHDRS=	algo.h algobase.h alloc.h backward_warning.h bvector.h
BKWHDRS+=	complex.h defalloc.h deque.h fstream.h function.h hash_map.h
BKWHDRS+=	hash_set.h hashtable.h heap.h iomanip.h iostream.h istream.h
BKWHDRS+=	iterator.h list.h map.h multimap.h multiset.h new.h ostream.h
BKWHDRS+=	pair.h queue.h rope.h set.h slist.h stack.h stream.h
BKWHDRS+=	streambuf.h strstream tempbuf.h tree.h vector.h
BKWHDRS:=	${BKWHDRS:S;^;${SRCDIR}/include/backward/;}
BKWHDRSDIR=	${INCLUDEDIR}/c++/3.4/backward

DEBUGHDRS=	bitset debug.h deque formatter.h hash_map hash_map.h
DEBUGHDRS+=	hash_multimap.h hash_multiset.h hash_set hash_set.h list map
DEBUGHDRS+=	map.h multimap.h multiset.h safe_base.h safe_iterator.h
DEBUGHDRS+=	safe_iterator.tcc safe_sequence.h set set.h string vector
DEBUGHDRS:=	${DEBUGHDRS:S;^;${SRCDIR}/include/debug/;}
DEBUGHDRSDIR=	${INCLUDEDIR}/c++/3.4/debug

EXTHDRS=	algorithm bitmap_allocator.h debug_allocator.h demangle.h
EXTHDRS+=	enc_filebuf.h functional hash_fun.h hash_map hash_set
EXTHDRS+=	hashtable.h iterator malloc_allocator.h memory mt_allocator.h
EXTHDRS+=	new_allocator.h numeric pod_char_traits.h pool_allocator.h
EXTHDRS+=	rb_tree rope ropeimpl.h slist stdio_filebuf.h
EXTHDRS+=	stdio_sync_filebuf.h
EXTHDRS:=	${EXTHDRS:S;^;${SRCDIR}/include/ext/;}
EXTHDRSDIR=	${INCLUDEDIR}/c++/3.4/ext

# This is the common subset of files that all three "C" header models use.
BASEHDRS=	std_cassert.h std_cctype.h std_cerrno.h std_cfloat.h
BASEHDRS+=	std_ciso646.h std_climits.h std_clocale.h std_cmath.h
BASEHDRS+=	std_csetjmp.h std_csignal.h std_cstdarg.h std_cstddef.h
BASEHDRS+=	std_cstdio.h std_cstdlib.h std_cstring.h std_ctime.h
BASEHDRS+=	std_cwchar.h std_cwctype.h
.for h in ${BASEHDRS}
BASEHDRSNAME_$h=${h:R:S;^std_;;}
.endfor
BASEHDRS:=	${BASEHDRS:S;^;${SRCDIR}/include/c_std/;}
BASEHDRSDIR=	${INCLUDEDIR}/c++/3.4

# Some of the different "C" header models need extra files.
BASEXHDRS=	${SRCDIR}/include/c_std/cmath.tcc
BASEXHDRSDIR=	${INCLUDEDIR}/c++/3.4/bits

STDHDRS=	std_algorithm.h std_bitset.h std_complex.h std_deque.h
STDHDRS+=	std_fstream.h std_functional.h std_iomanip.h std_ios.h
STDHDRS+=	std_iosfwd.h std_iostream.h std_istream.h std_iterator.h
STDHDRS+=	std_limits.h std_list.h std_locale.h std_map.h std_memory.h
STDHDRS+=	std_numeric.h std_ostream.h std_queue.h std_set.h
STDHDRS+=	std_sstream.h std_stack.h std_stdexcept.h std_streambuf.h
STDHDRS+=	std_string.h std_utility.h std_valarray.h std_vector.h
.for h in ${STDHDRS}
STDHDRSNAME_$h=${h:R:S;^std_;;}
.endfor
STDHDRS:=	${STDHDRS:S;^;${SRCDIR}/include/std/;}
STDHDRSDIR=	${INCLUDEDIR}/c++/3.4

# c++locale <- c_locale.h
TARGETHDRS=	io/basic_file_stdio.h io/c_io_stdio.h
TARGETHDRS+=	allocator/new_allocator_base.h
TARGETHDRS+=	os/bsd/freebsd/ctype_inline.h os/bsd/freebsd/ctype_noninline.h
TARGETHDRS+=	os/bsd/freebsd/os_defines.h os/bsd/freebsd/ctype_base.h
TARGETHDRS+=	locale/generic/messages_members.h locale/generic/time_members.h
TARGETHDRS+=	locale/generic/codecvt_specializations.h
TARGETHDRS+=	locale/generic/c_locale.h
TARGETHDRS:=	${TARGETHDRS:S;^;${SRCDIR}/config/;}

TARGETHDRSNAME_c_io_stdio.h=		c++io.h
TARGETHDRSNAME_new_allocator_base.h=	c++allocator.h
TARGETHDRSNAME_basic_file_stdio.h=	basic_file.h
TARGETHDRSNAME_c_locale.h=		c++locale.h
TARGETHDRS+=	${.CURDIR}/c++config.h
TARGETHDRSDIR=	${INCLUDEDIR}/c++/3.4/bits

MARCHHDRS=	atomic_word.h

.for h in ${MARCHHDRS}
.if exists(${SRCDIR}/config/cpu/${MARCHDIR}/${h})
TARGETHDRS+=	${SRCDIR}/config/cpu/${MARCHDIR}/${h}
.else
TARGETHDRS+=	${SRCDIR}/config/cpu/generic/${h}
.endif
.endfor

THRHDRS=	gthr.h gthr-single.h gthr-posix.h gthr-default.h
THRHDRSDIR=	${INCLUDEDIR}/c++/3.4/bits

gthr.h: ${GCCDIR}/gcc/gthr.h
	sed -e '/^#/s/\([ABCDEFGHIJKLMNOPQRSTUVWXYZ_][ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*\)/_GLIBCPP_\1/g' \
	    -e 's,^#include "\(.*\)",#include <bits/\1>,g' \
	    < ${GCCDIR}/gcc/gthr.h > gthr.h

gthr-single.h: ${GCCDIR}/gcc/gthr-single.h
	sed -e 's/\(UNUSED\)/_GLIBCPP_\1/g' \
	    -e 's/\(GCC[ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*_H\)/_GLIBCPP_\1/g' \
	    < ${GCCDIR}/gcc/gthr-single.h > gthr-single.h

gthr-posix.h: ${GCCDIR}/gcc/gthr-posix.h
	sed -e 's/\(UNUSED\)/_GLIBCPP_\1/g' \
	    -e 's/\(GCC[ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*_H\)/_GLIBCPP_\1/g' \
	    -e 's/\([ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*WEAK\)/_GLIBCPP_\1/g' \
	    < ${GCCDIR}/gcc/gthr-posix.h > gthr-posix.h

gthr-default.h: ${GCCDIR}/gcc/gthr-posix.h
	sed -e 's/\(UNUSED\)/_GLIBCPP_\1/g' \
	    -e 's/\(GCC[ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*_H\)/_GLIBCPP_\1/g' \
	    -e 's/\([ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*WEAK\)/_GLIBCPP_\1/g' \
	    -e 's,^#include "\(.*\)",#include <bits/\1>,g' \
	    < ${GCCDIR}/gcc/gthr-posix.h > gthr-default.h

CLEANFILES+=	${THRHDRS}

.include <bsd.lib.mk>
