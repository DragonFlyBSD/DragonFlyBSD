.include "../Makefile.inc"
.include "../libsupc++/Makefile.src"
.include "Makefile.headers"
SRCDIR=	${GCCDIR}/libstdc++-v3
.PATH:	${SRCDIR}
.PATH:	${SRCDIR}/libsupc++
.PATH:	${SRCDIR}/src
.PATH:	${SRCDIR}/src/c++11
.PATH:	${SRCDIR}/src/c++98
.PATH:	${SRCDIR}/config/io
.PATH:	${SRCDIR}/config/locale/dragonfly
.PATH:	${SRCDIR}/config/locale/generic
.PATH:	${GCCDIR}/gcc
.PATH:	${GCCDIR}/libgcc

# Vendor lists shared lib version as 6, not 9.
LIB=		stdc++
SHLIB_MAJOR=	9

CFLAGS+=	-DIN_GLIBCPP_V3 -DHAVE_CONFIG_H
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${GCCDIR}/gcc
CFLAGS+=	-I${GCCDIR}/libgcc
CFLAGS+=	-I${GCCDIR}/libiberty
CFLAGS+=	-I${GCCDIR}/libcpp/include
CXXFLAGS+=	-fno-implicit-templates
CXXFLAGS+=	-fdiagnostics-show-location=once
CXXFLAGS+=	-ffunction-sections -fdata-sections
CXXFLAGS+=	-Wno-deprecated
LDFLAGS+=	-Wl,--version-script=${.CURDIR}/libstdc++-symbols.ver

THRLIB= 	${.OBJDIR}/../../../../lib/libpthread/libpthread.so
LDADD+= 	${THRLIB} -lm
DPADD+= 	${THRLIB} ${LIBM}

# Slightly modified from libstdc++/src/c++98/Makefile.in
# sources_1998 is the equivalent of libc_98convenience.a
# sources_2011 is the equivalent of libc_11convenience.a
host_sources_1998 = \
	atomicity.cc \
	codecvt_members.cc \
	collate_members.cc \
	ctype_configure_char.cc \
	ctype_members.cc \
	messages_members.cc \
	monetary_members.cc \
	numeric_members.cc \
	time_members.cc

parallel_sources_1998 =

inst_sources_1998 = \
	allocator-inst.cc \
	concept-inst.cc \
	ext-inst.cc \
	ios-inst.cc \
	iostream-inst.cc \
	istream-inst.cc \
	locale-inst.cc \
	misc-inst.cc \
	ostream-inst.cc \
	sstream-inst.cc \
	streambuf-inst.cc \
	wlocale-inst.cc

host_sources_extra_1998 = \
	basic_file.cc \
	c++locale.cc \
	${inst_sources_1998} \
	${parallel_sources_1998}

sources_1998 = \
	bitmap_allocator.cc \
	pool_allocator.cc \
	mt_allocator.cc \
	codecvt.cc \
	complex_io.cc \
	ctype.cc \
	globals_io.cc \
	hash_tr1.cc \
	hashtable_tr1.cc \
	ios.cc \
	ios_failure.cc \
	ios_init.cc \
	ios_locale.cc \
	list.cc \
	locale.cc \
	locale_init.cc \
	locale_facets.cc \
	localename.cc \
	math_stubs_float.cc \
	math_stubs_long_double.cc \
	stdexcept.cc \
	strstream.cc \
	tree-cxx.cc \
	istream.cc \
	streambuf.cc \
	valarray.cc \
	${host_sources_1998} \
	${host_sources_extra_1998}

inst_sources_2011 = \
	fstream-inst.cc \
	string-inst.cc \
	wstring-inst.cc

sources_2011 = \
	chrono.cc \
	condition_variable.cc \
	debug-cxx.cc \
	functexcept.cc \
	functional.cc \
	future.cc \
	hash_c++0x.cc \
	hashtable_c++0x.cc \
	limits.cc \
	mutex.cc \
	placeholders.cc \
	regex.cc \
	shared_ptr.cc \
	system_error.cc \
	thread.cc \
	${inst_sources_2011}

ldbl_compat_sources = 
parallel_compat_sources = 

cxx98_sources = \
	compatibility.cc \
	compatibility-debug_list.cc \
	compatibility-debug_list-2.cc \
	compatibility-list.cc \
	compatibility-list-2.cc \
	${ldbl_compat_sources} \
	${parallel_compat_sources}

cxx11_sources = \
	compatibility-c++0x.cc \
	compatibility-atomic-c++0x.cc \
	compatibility-thread-c++0x.cc

SRCS+=	${SUPCXX} \
	cp-demangle.c \
	${sources_1998} \
	${sources_2011} \
	${cxx98_sources} \
	${cxx11_sources}

FLAGS_GROUPS=		gnu0x gnu11
gnu0x_FLAGS=		-std=gnu++0x
gnu11_FLAGS=		-std=gnu++11
gnu0x_FLAGS_FILES=	eh_ptr.cc \
			eh_throw.cc \
			guard.cc \
			nested_exception.cc
gnu11_FLAGS_FILES=	chrono.cc \
			condition_variable.cc \
			debug-cxx.cc \
			functexcept.cc \
			functional.cc \
			future.cc \
			hash_c++0x.cc \
			hashtable_c++0x.cc \
			limits.cc \
			mutex.cc \
			placeholders.cc \
			regex.cc \
			shared_ptr.cc \
			system_error.cc \
			thread.cc \
			fstream-inst.cc \
			string-inst.cc \
			wstring-inst.cc \
			compatibility-c++0x.cc \
			compatibility-atomic-c++0x.cc \
			compatibility-thread-c++0x.cc

#generated sources
SRCS+=	unwind.h \
	unwind-cxx.h \
	cxxabi_forced.h

HD=	${INCLUDEDIR}/c++/${GCCPOINTVER}

# These sections are detailed in Makefile.headers
# parallel section is empty, skip it
# make buildincludes, make installincludes before make depend
glibcxx_srcdir=	${SRCDIR}
HSECT=	std bits bits_sup backward ext ext_compat tr1 tr2 decimal \
	c_base c_compatibility debug profile profile_impl host supc
PBSECT=	1 2 3 4 5 6 7
	
.for i in ${HSECT}
INCSGROUPS+=	 	${i}_headers
${i}_headersDIR= 	${HD}/${${i}_builddir}
.endfor

PATHGROUP=
.for i in ${PBSECT}
.  for k in ${pb_headers${i}}
.    if ${PATHGROUP:M${k:H:T}} == ""
PATHGROUP+=${k:H:T}
PATH_${k:H:T}=${k:H:T}
.    endif
PBG_${k:H:T}+=${k}
.  endfor
.endfor

INCSGROUPS+=		PBG_pb_ds
PBG_pb_dsDIR=		${HD}/${pb_builddir}

INCSGROUPS+=		PBG_detail
PBG_detailDIR=		${HD}/${pb_builddir}/detail

.for k in ${PATHGROUP:Npb_ds:Ndetail}
INCSGROUPS+=		PBG_${k}
PBG_${k}DIR=		${HD}/${pb_builddir}/detail/${PATH_${k}}
.endfor

INCSGROUPS+=		host_headers_extra
host_headers_extraDIR=	${HD}/bits
host_headers_extraNAME_basic_file_stdio.h=	basic_file.h
host_headers_extraNAME_c_locale.h=		c++locale.h
host_headers_extraNAME_c_io_stdio.h+=		c++io.h
host_headers_extraNAME_new_allocator_base.h=	c++allocator.h

atomicity.cc: ${SRCDIR}/config/cpu/generic/atomicity_builtins/atomicity.h
	cp ${.ALLSRC} ${.TARGET}

ctype_configure_char.cc: ${SRCDIR}/config/os/bsd/dragonfly/${.TARGET}
	cp ${.ALLSRC} ${.TARGET}

basic_file.cc: ${SRCDIR}/config/io/basic_file_stdio.cc
	cp ${.ALLSRC} ${.TARGET}

c++locale.cc: ${SRCDIR}/config/locale/dragonfly/c_locale.cc
	cp ${.ALLSRC} ${.TARGET}

unwind.h: unwind-generic.h
	cp ${.ALLSRC} ${.TARGET}

unwind-cxx.h: ${SRCDIR}/libsupc++/unwind-cxx.h
	cp ${.ALLSRC} ${.TARGET}

cxxabi_forced.h: ${SRCDIR}/libsupc++/cxxabi_forced.h
	cp ${.ALLSRC} ${.TARGET}

# We can't add libiberty to the PATH because it will take the regex.o there
# before compiling regex.cc into an object file.  Just recompile

cp-demangle.c: ${GCCDIR}/libiberty/${.TARGET}
	cp ${.ALLSRC} ${.TARGET}

# debug.cc, tree.cc and vec.cc are also getting pre-empted by gcc/ versions
debug-cxx.cc: ${SRCDIR}/src/c++11/debug.cc
	cp ${.ALLSRC} ${.TARGET}

tree-cxx.cc: ${SRCDIR}/src/c++98/tree.cc
	cp ${.ALLSRC} ${.TARGET}

vec-cxx.cc: ${SRCDIR}/libsupc++/vec.cc
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	atomicity.cc basic_file.cc c++locale.cc \
		ctype_configure_char.cc unwind.h unwind-cxx.h \
		cp-demangle.c debug-cxx.cc tree-cxx.cc vec-cxx.cc \
		cxxabi_forced.h c++config.h gthr.h gthr-default.h

.include <bsd.lib.mk>
