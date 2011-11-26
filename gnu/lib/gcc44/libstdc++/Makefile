.include "../Makefile.inc"

.if ${MACHINE_ARCH} == "i386" && !empty(MACHINE_CPU:Mi486)
MARCHDIR=	i486
.elif ${MACHINE_ARCH} == "x86_64"
MARCHDIR=	i486
.else
MARCHDIR=	${MACHINE_ARCH}
.endif

SRCDIR=	${GCCDIR}/libstdc++-v3

glibcxx_srcdir=	${SRCDIR}

.PATH:	${SRCDIR}/src ${SRCDIR}/libmath ${SRCDIR}/config/io
.PATH:	${SRCDIR}/config/locale/dragonfly
.PATH:	${SRCDIR}/config/locale/generic ${SRCDIR}
.PATH:	${GCCDIR}/gcc ${GCCDIR}/libiberty

#SUBDIR=	doc

LIB=		stdc++
SHLIB_MAJOR=	9

CFLAGS+=	-DIN_GLIBCPP_V3 -DHAVE_CONFIG_H
CFLAGS+=	-I${GCCDIR}/gcc -I${GCCDIR}/include
CXXFLAGS+=	-fno-implicit-templates -ffunction-sections -fdata-sections \
		-Wno-deprecated

FLAGS_GROUPS=	c++0x
c++0x_FLAGS=	-std=gnu++0x
c++0x_FLAGS_FILES= \
	functexcept.cc system_error.cc mutex.cc condition_variable.cc \
	hash_c++0x.cc hashtable_c++0x.cc limits_c++0x.cc atomic.cc \
	string-inst.cc wstring-inst.cc chrono.cc thread.cc

LDADD+=		-lm
DPADD+=		${LIBM}

.include "../libsupc++/Makefile.src"

# libstdc++ sources (libstdc++/src/Makefile.in, do not reorder)
SRCS+=	\
	atomic.cc \
	bitmap_allocator.cc \
	pool_allocator.cc \
	mt_allocator.cc \
	codecvt.cc \
	compatibility.cc \
	complex_io.cc \
	ctype.cc \
	debug.cc \
	functexcept.cc \
	hash.cc \
	hash_c++0x.cc \
	globals_io.cc \
	hashtable.cc \
	hashtable_c++0x.cc \
	ios.cc \
	ios_failure.cc \
	ios_init.cc \
	ios_locale.cc \
	limits.cc \
	limits_c++0x.cc \
	list.cc \
	debug_list.cc \
	locale.cc \
	locale_init.cc \
	locale_facets.cc \
	localename.cc \
	math_stubs_float.cc \
	math_stubs_long_double.cc \
	stdexcept.cc \
	strstream.cc \
	system_error.cc \
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
	misc-inst.cc \
	ostream-inst.cc \
	sstream-inst.cc \
	streambuf-inst.cc \
	streambuf.cc \
	string-inst.cc \
	valarray-inst.cc \
	wlocale-inst.cc \
	wstring-inst.cc \
	mutex.cc \
	condition_variable.cc \
	chrono.cc \
	thread.cc \
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

# Here we change the file to stdio and c++locale to c_locale
host_sources_extra = \
	basic_file_stdio.cc c_locale.cc

atomicity.cc:
	echo '#include <ext/atomicity.h>' > ${.TARGET}

CLEANFILES+=	atomicity.cc

SRCS+=	gstdint.h bits/compatibility.h

gstdint.h:
	echo "#include <stdint.h>" > ${.TARGET}

bits/compatibility.h: config/abi/compatibility.h
	mkdir -p ${.TARGET:H}
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	gstdint.h bits/compatibility.h
CLEANDIRS+=	bits


strstream.cc:	${SRCDIR}/src/strstream.cc
	sed -e 's,^#include <strstream>,#include <backward/strstream>,g' \
	    < ${.ALLSRC} > ${.TARGET}

CLEANFILES+=	strstream.cc


HD=	${INCLUDEDIR}/c++/${GCCPOINTVER}
.for i in std bits backward ext tr1 tr1_impl c_base c_compatibility debug \
	c_base_headers_extra

INCSGROUPS+=	$i_headers
$i_headersDIR=	${HD}/${$i_builddir}
.endfor

INCSGROUPS+=	TARGETHDRS THRHDRS

std_srcdir = ${glibcxx_srcdir}/include/std
std_builddir = .
std_headers = \
	${std_srcdir}/algorithm \
	${std_srcdir}/array \
	${std_srcdir}/bitset \
	${std_srcdir}/c++0x_warning.h \
	${std_srcdir}/chrono \
	${std_srcdir}/complex \
	${std_srcdir}/condition_variable \
	${std_srcdir}/deque \
	${std_srcdir}/forward_list \
	${std_srcdir}/fstream \
	${std_srcdir}/functional \
	${std_srcdir}/iomanip \
	${std_srcdir}/ios \
	${std_srcdir}/iosfwd \
	${std_srcdir}/iostream \
	${std_srcdir}/istream \
	${std_srcdir}/iterator \
	${std_srcdir}/limits \
	${std_srcdir}/list \
	${std_srcdir}/locale \
	${std_srcdir}/map \
	${std_srcdir}/memory \
	${std_srcdir}/mutex \
	${std_srcdir}/numeric \
	${std_srcdir}/ostream \
	${std_srcdir}/queue \
	${std_srcdir}/random \
	${std_srcdir}/ratio \
	${std_srcdir}/regex \
	${std_srcdir}/set \
	${std_srcdir}/sstream \
	${std_srcdir}/stack \
	${std_srcdir}/stdexcept \
	${std_srcdir}/streambuf \
	${std_srcdir}/string \
	${std_srcdir}/system_error \
	${std_srcdir}/thread \
	${std_srcdir}/tuple \
	${std_srcdir}/type_traits \
	${std_srcdir}/unordered_map \
	${std_srcdir}/unordered_set \
	${std_srcdir}/utility \
	${std_srcdir}/valarray \
	${std_srcdir}/vector


bits_srcdir = ${glibcxx_srcdir}/include/bits
bits_builddir = ./bits
bits_headers = \
        ${bits_srcdir}/algorithmfwd.h \
	${bits_srcdir}/allocator.h \
	${bits_srcdir}/atomicfwd_c.h \
	${bits_srcdir}/atomicfwd_cxx.h \
	${bits_srcdir}/atomic_0.h \
	${bits_srcdir}/atomic_2.h \
	${bits_srcdir}/basic_ios.h \
	${bits_srcdir}/basic_ios.tcc \
	${bits_srcdir}/basic_string.h \
	${bits_srcdir}/basic_string.tcc \
	${bits_srcdir}/boost_concept_check.h \
	${bits_srcdir}/char_traits.h \
	${bits_srcdir}/codecvt.h \
	${bits_srcdir}/concept_check.h \
	${bits_srcdir}/cpp_type_traits.h \
	${bits_srcdir}/deque.tcc \
	${bits_srcdir}/forward_list.h \
	${bits_srcdir}/forward_list.tcc \
	${bits_srcdir}/fstream.tcc \
	${bits_srcdir}/functexcept.h \
	${bits_srcdir}/functional_hash.h \
	${bits_srcdir}/gslice.h \
	${bits_srcdir}/gslice_array.h \
	${bits_srcdir}/hashtable.h \
	${bits_srcdir}/indirect_array.h \
	${bits_srcdir}/ios_base.h \
	${bits_srcdir}/istream.tcc \
	${bits_srcdir}/list.tcc \
	${bits_srcdir}/locale_classes.h \
	${bits_srcdir}/locale_classes.tcc \
	${bits_srcdir}/locale_facets.h \
	${bits_srcdir}/locale_facets.tcc \
	${bits_srcdir}/locale_facets_nonio.h \
	${bits_srcdir}/locale_facets_nonio.tcc \
	${bits_srcdir}/localefwd.h \
	${bits_srcdir}/mask_array.h \
	${bits_srcdir}/move.h \
	${bits_srcdir}/ostream.tcc \
	${bits_srcdir}/ostream_insert.h \
	${bits_srcdir}/postypes.h \
	${bits_srcdir}/stream_iterator.h \
	${bits_srcdir}/streambuf_iterator.h \
	${bits_srcdir}/shared_ptr.h \
	${bits_srcdir}/slice_array.h \
	${bits_srcdir}/sstream.tcc \
	${bits_srcdir}/stl_algo.h \
	${bits_srcdir}/stl_algobase.h \
	${bits_srcdir}/stl_bvector.h \
	${bits_srcdir}/stl_construct.h \
	${bits_srcdir}/stl_deque.h \
	${bits_srcdir}/stl_function.h \
	${bits_srcdir}/stl_heap.h \
	${bits_srcdir}/stl_iterator.h \
	${bits_srcdir}/stl_iterator_base_funcs.h \
	${bits_srcdir}/stl_iterator_base_types.h \
	${bits_srcdir}/stl_list.h \
	${bits_srcdir}/stl_map.h \
	${bits_srcdir}/stl_multimap.h \
	${bits_srcdir}/stl_multiset.h \
	${bits_srcdir}/stl_numeric.h \
	${bits_srcdir}/stl_pair.h \
	${bits_srcdir}/stl_queue.h \
	${bits_srcdir}/stl_raw_storage_iter.h \
	${bits_srcdir}/stl_relops.h \
	${bits_srcdir}/stl_set.h \
	${bits_srcdir}/stl_stack.h \
	${bits_srcdir}/stl_tempbuf.h \
	${bits_srcdir}/stl_tree.h \
	${bits_srcdir}/stl_uninitialized.h \
	${bits_srcdir}/stl_vector.h \
	${bits_srcdir}/streambuf.tcc \
	${bits_srcdir}/stringfwd.h \
	${bits_srcdir}/unique_ptr.h \
	${bits_srcdir}/valarray_array.h \
	${bits_srcdir}/valarray_array.tcc \
	${bits_srcdir}/valarray_before.h \
	${bits_srcdir}/valarray_after.h \
	${bits_srcdir}/vector.tcc


backward_srcdir = ${glibcxx_srcdir}/include/backward
backward_builddir = ./backward
backward_headers = \
	${backward_srcdir}/auto_ptr.h \
	${backward_srcdir}/backward_warning.h \
	${backward_srcdir}/binders.h \
	${backward_srcdir}/hash_map \
	${backward_srcdir}/hash_set \
	${backward_srcdir}/hash_fun.h \
	${backward_srcdir}/hashtable.h \
	${backward_srcdir}/strstream


ext_srcdir = ${glibcxx_srcdir}/include/ext
ext_builddir = ./ext
ext_headers = \
	${ext_srcdir}/algorithm \
	${ext_srcdir}/atomicity.h \
	${ext_srcdir}/array_allocator.h \
	${ext_srcdir}/bitmap_allocator.h \
	${ext_srcdir}/cast.h \
	${ext_srcdir}/codecvt_specializations.h \
	${ext_srcdir}/concurrence.h \
	${ext_srcdir}/debug_allocator.h \
	${ext_srcdir}/enc_filebuf.h \
	${ext_srcdir}/extptr_allocator.h \
	${ext_srcdir}/stdio_filebuf.h \
	${ext_srcdir}/stdio_sync_filebuf.h \
	${ext_srcdir}/functional \
	${ext_srcdir}/iterator \
	${ext_srcdir}/malloc_allocator.h \
	${ext_srcdir}/memory \
	${ext_srcdir}/mt_allocator.h \
	${ext_srcdir}/new_allocator.h \
	${ext_srcdir}/numeric \
	${ext_srcdir}/numeric_traits.h \
	${ext_srcdir}/pod_char_traits.h \
	${ext_srcdir}/pointer.h \
	${ext_srcdir}/pool_allocator.h \
	${ext_srcdir}/rb_tree \
	${ext_srcdir}/rope \
	${ext_srcdir}/ropeimpl.h \
	${ext_srcdir}/slist \
	${ext_srcdir}/string_conversions.h \
	${ext_srcdir}/throw_allocator.h \
	${ext_srcdir}/typelist.h \
	${ext_srcdir}/type_traits.h \
	${ext_srcdir}/rc_string_base.h \
	${ext_srcdir}/sso_string_base.h \
	${ext_srcdir}/vstring.h \
	${ext_srcdir}/vstring.tcc \
	${ext_srcdir}/vstring_fwd.h \
	${ext_srcdir}/vstring_util.h \
	${ext_compat_headers}
ext_compat_headers = \
	${backward_srcdir}/hash_set \
	${backward_srcdir}/hash_map


tr1_srcdir = ${glibcxx_srcdir}/include/tr1
tr1_builddir = ./tr1
tr1_headers = \
	${tr1_srcdir}/array \
	${tr1_srcdir}/bessel_function.tcc \
	${tr1_srcdir}/beta_function.tcc \
	${tr1_srcdir}/ccomplex \
	${tr1_srcdir}/cctype \
	${tr1_srcdir}/cfenv \
	${tr1_srcdir}/cfloat \
	${tr1_srcdir}/cinttypes \
	${tr1_srcdir}/climits \
	${tr1_srcdir}/cmath \
	${tr1_srcdir}/complex \
	${tr1_srcdir}/complex.h \
	${tr1_srcdir}/cstdarg \
	${tr1_srcdir}/cstdbool \
	${tr1_srcdir}/cstdint \
	${tr1_srcdir}/cstdio \
	${tr1_srcdir}/cstdlib \
	${tr1_srcdir}/ctgmath \
	${tr1_srcdir}/ctime \
	${tr1_srcdir}/ctype.h \
	${tr1_srcdir}/cwchar \
	${tr1_srcdir}/cwctype \
	${tr1_srcdir}/ell_integral.tcc \
	${tr1_srcdir}/exp_integral.tcc \
	${tr1_srcdir}/fenv.h \
	${tr1_srcdir}/float.h \
	${tr1_srcdir}/functional \
	${tr1_srcdir}/functional_hash.h \
	${tr1_srcdir}/gamma.tcc \
	${tr1_srcdir}/hypergeometric.tcc \
	${tr1_srcdir}/hashtable.h \
	${tr1_srcdir}/inttypes.h \
	${tr1_srcdir}/limits.h \
	${tr1_srcdir}/math.h \
	${tr1_srcdir}/memory \
	${tr1_srcdir}/modified_bessel_func.tcc \
	${tr1_srcdir}/poly_hermite.tcc \
	${tr1_srcdir}/poly_laguerre.tcc \
	${tr1_srcdir}/legendre_function.tcc \
	${tr1_srcdir}/random \
	${tr1_srcdir}/regex \
	${tr1_srcdir}/riemann_zeta.tcc \
	${tr1_srcdir}/shared_ptr.h \
	${tr1_srcdir}/special_function_util.h \
	${tr1_srcdir}/stdarg.h \
	${tr1_srcdir}/stdbool.h \
	${tr1_srcdir}/stdint.h \
	${tr1_srcdir}/stdio.h \
	${tr1_srcdir}/stdlib.h \
	${tr1_srcdir}/tgmath.h \
	${tr1_srcdir}/tuple \
	${tr1_srcdir}/type_traits \
	${tr1_srcdir}/unordered_set \
	${tr1_srcdir}/unordered_map \
	${tr1_srcdir}/utility \
	${tr1_srcdir}/wchar.h \
	${tr1_srcdir}/wctype.h


tr1_impl_srcdir = ${glibcxx_srcdir}/include/tr1_impl
tr1_impl_builddir = ./tr1_impl
tr1_impl_headers = \
	${tr1_impl_srcdir}/array \
	${tr1_impl_srcdir}/boost_sp_counted_base.h \
	${tr1_impl_srcdir}/cctype \
	${tr1_impl_srcdir}/cfenv \
	${tr1_impl_srcdir}/cinttypes \
	${tr1_impl_srcdir}/cmath \
	${tr1_impl_srcdir}/complex \
	${tr1_impl_srcdir}/cstdint \
	${tr1_impl_srcdir}/cstdio \
	${tr1_impl_srcdir}/cstdlib \
	${tr1_impl_srcdir}/cwchar \
	${tr1_impl_srcdir}/cwctype \
	${tr1_impl_srcdir}/functional \
	${tr1_impl_srcdir}/functional_hash.h \
	${tr1_impl_srcdir}/hashtable \
	${tr1_impl_srcdir}/hashtable_policy.h \
	${tr1_impl_srcdir}/random \
	${tr1_impl_srcdir}/random.tcc \
	${tr1_impl_srcdir}/regex \
	${tr1_impl_srcdir}/type_traits \
	${tr1_impl_srcdir}/unordered_map \
	${tr1_impl_srcdir}/unordered_set \
	${tr1_impl_srcdir}/utility

C_INCLUDE_DIR = ${glibcxx_srcdir}/include/c_global
c_base_srcdir = $(C_INCLUDE_DIR)
c_base_builddir = .
c_base_headers = \
	${c_base_srcdir}/cassert \
	${c_base_srcdir}/ccomplex \
	${c_base_srcdir}/cctype \
	${c_base_srcdir}/cerrno \
	${c_base_srcdir}/cfenv \
	${c_base_srcdir}/cfloat \
	${c_base_srcdir}/cinttypes \
	${c_base_srcdir}/ciso646 \
	${c_base_srcdir}/climits \
	${c_base_srcdir}/clocale \
	${c_base_srcdir}/cmath \
	${c_base_srcdir}/csetjmp \
	${c_base_srcdir}/csignal \
	${c_base_srcdir}/cstdarg \
	${c_base_srcdir}/cstdatomic \
	${c_base_srcdir}/cstdbool \
	${c_base_srcdir}/cstddef \
	${c_base_srcdir}/cstdint \
	${c_base_srcdir}/cstdio \
	${c_base_srcdir}/cstdlib \
	${c_base_srcdir}/cstring \
	${c_base_srcdir}/ctgmath \
	${c_base_srcdir}/ctime \
	${c_base_srcdir}/cwchar \
	${c_base_srcdir}/cwctype

c_compatibility_srcdir = ${glibcxx_srcdir}/include/c_compatibility
c_compatibility_builddir = .
c_compatibility_headers = \
	${c_compatibility_srcdir}/complex.h \
	${c_compatibility_srcdir}/fenv.h \
	${c_compatibility_srcdir}/tgmath.h \
	${c_compatibility_srcdir}/stdatomic.h


debug_srcdir = ${glibcxx_srcdir}/include/debug
debug_builddir = ./debug
debug_headers = \
	${debug_srcdir}/bitset \
	${debug_srcdir}/debug.h \
	${debug_srcdir}/deque \
	${debug_srcdir}/formatter.h \
	${debug_srcdir}/functions.h \
	${debug_srcdir}/list \
	${debug_srcdir}/map \
	${debug_srcdir}/macros.h \
	${debug_srcdir}/map.h \
	${debug_srcdir}/multimap.h \
	${debug_srcdir}/multiset.h \
	${debug_srcdir}/safe_base.h \
	${debug_srcdir}/safe_iterator.h \
	${debug_srcdir}/safe_iterator.tcc \
	${debug_srcdir}/safe_sequence.h \
	${debug_srcdir}/set \
	${debug_srcdir}/set.h \
	${debug_srcdir}/string \
	${debug_srcdir}/unordered_map \
	${debug_srcdir}/unordered_set \
	${debug_srcdir}/vector


c_base_headers_extra_builddir = bits
c_base_headers_extra_headers = ${c_base_srcdir}/cmath.tcc


host_srcdir = ${glibcxx_srcdir}/$(OS_INC_SRCDIR)
host_builddir = ./${host_alias}/bits
host_installdir = ${gxx_include_dir}/${host_alias}$(MULTISUBDIR)/bits
host_headers = \
	${host_srcdir}/ctype_base.h \
	${host_srcdir}/ctype_inline.h \
	${host_srcdir}/ctype_noninline.h \
	${host_srcdir}/os_defines.h \
	${glibcxx_srcdir}/$(ATOMIC_WORD_SRCDIR)/atomic_word.h \
	${glibcxx_srcdir}/$(ABI_TWEAKS_SRCDIR)/cxxabi_tweaks.h \
	${glibcxx_srcdir}/$(CPU_DEFINES_SRCDIR)/cpu_defines.h \
	${glibcxx_srcdir}/$(ERROR_CONSTANTS_SRCDIR)/error_constants.h \
	${glibcxx_srcdir}/include/precompiled/stdc++.h \
	${glibcxx_srcdir}/include/precompiled/stdtr1c++.h \
	${glibcxx_srcdir}/include/precompiled/extc++.h

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

TARGETHDRS+=	os/generic/error_constants.h
TARGETHDRS+=	os/bsd/dragonfly/ctype_inline.h
TARGETHDRS+=	os/bsd/dragonfly/ctype_base.h
TARGETHDRS+=	os/bsd/dragonfly/ctype_noninline.h
TARGETHDRS+=	os/bsd/dragonfly/os_defines.h
TARGETHDRS:=	${TARGETHDRS:S;^;${SRCDIR}/config/;}
TARGETHDRS+=	c++config.h
TARGETHDRSDIR=	${HD}/bits

MARCHHDRS=	atomic_word.h cxxabi_tweaks.h cpu_defines.h

.for h in ${MARCHHDRS}
.if exists(${SRCDIR}/config/cpu/${MARCHDIR}/${h})
TARGETHDRS+=	${SRCDIR}/config/cpu/${MARCHDIR}/${h}
.else
TARGETHDRS+=	${SRCDIR}/config/cpu/generic/${h}
.endif
.endfor

THRHDRS=	gthr.h gthr-single.h gthr-posix.h gthr-default.h
THRHDRSDIR=	${HD}/bits

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

c++config.h: config.h ${SRCDIR}/include/bits/c++config DATESTAMP
	date=`cat ${.ALLSRC:M*DATESTAMP}` ; \
	nsa_version=0 ;\
	visibility=1 ;\
	sed -e "s,define __GLIBCXX__, define __GLIBCXX__ $$date," \
	-e "s,define _GLIBCXX_NAMESPACE_ASSOCIATION_VERSION, define _GLIBCXX_NAMESPACE_ASSOCIATION_VERSION $$nsa_version," \
	-e "s,define _GLIBCXX_HAVE_ATTRIBUTE_VISIBILITY, define _GLIBCXX_HAVE_ATTRIBUTE_VISIBILITY $$visibility," \
	< ${.ALLSRC:M*c++config} > ${.TARGET}
	sed -e 's/HAVE_/_GLIBCXX_HAVE_/g' \
	    -e 's/PACKAGE/_GLIBCXX_PACKAGE/g' \
	    -e 's/VERSION/_GLIBCXX_VERSION/g' \
	    -e 's/WORDS_/_GLIBCXX_WORDS_/g' \
	    -e 's/ICONV_CONST/_GLIBCXX_ICONV_CONST/g' \
	    -e '/[ 	]_GLIBCXX_LONG_DOUBLE_COMPAT[ 	]/d' \
	    < ${.ALLSRC:M*config.h} >> ${.TARGET}
	echo "" >> ${.TARGET}
	echo "#endif // _GLIBCXX_CXX_CONFIG_H" >> ${.TARGET}

CLEANFILES+=	${THRHDRS} c++config.h

.include <bsd.lib.mk>
