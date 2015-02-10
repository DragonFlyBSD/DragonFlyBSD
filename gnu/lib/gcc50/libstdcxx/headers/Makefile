RELUP=	/..
.include "../../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.headers"

.PATH:	${SRCDIR}

# These sections are detailed in Makefile.headers
# parallel section is empty, skip it
# make buildincludes, make installincludes before make depend
glibcxx_srcdir=	${SRCDIR}
HSECT=	std bits bits_sup backward ext ext_compat tr1 tr2 decimal \
	c_base c_compatibility debug profile profile_impl host supc \
	parallel experimental ext_host
PBSECT=	1 2 3 4 5 6 7
HD=	${INCLUDEDIR}/c++/${GCCPOINTVER}
	
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

unwind.h: unwind-generic.h
	cp ${.ALLSRC} ${.TARGET}

unwind-cxx.h: ${SRCDIR}/libsupc++/unwind-cxx.h
	cp ${.ALLSRC} ${.TARGET}

cxxabi_forced.h: ${SRCDIR}/libsupc++/cxxabi_forced.h
	cp ${.ALLSRC} ${.TARGET}

gthr.h: ${GCCDIR}/libgcc/gthr.h
	sed -e '/^#pragma/b' \
	    -e '/^#/s/\([ABCDEFGHIJKLMNOPQRSTUVWXYZ_][ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*\)/_GLIBCXX_\1/g' \
	    -e 's/_GLIBCXX_SUPPORTS_WEAK/__GXX_WEAK__/g' \
	    -e 's/_GLIBCXX___MINGW32_GLIBCXX___/__MINGW32__/g' \
	    -e 's,^#include "\(.*\)",#include <bits/\1>,g' \
    	< ${GCCDIR}/libgcc/gthr.h > ${.TARGET}

gthr-single.h: ${GCCDIR}/libgcc/gthr.h
	sed -e 's/\(UNUSED\)/_GLIBCXX_\1/g' \
	    -e 's/\(GCC[ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*_H\)/_GLIBCXX_\1/g' \
	< ${GCCDIR}/libgcc/gthr-single.h > ${.TARGET}

gthr-posix.h: ${GCCDIR}/libgcc/gthr-posix.h
	sed -e 's/\(UNUSED\)/_GLIBCXX_\1/g' \
	    -e 's/\(GCC[ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*_H\)/_GLIBCXX_\1/g' \
	    -e 's/SUPPORTS_WEAK/__GXX_WEAK__/g' \
	    -e 's/\([ABCDEFGHIJKLMNOPQRSTUVWXYZ_]*USE_WEAK\)/_GLIBCXX_\1/g' \
	    < ${GCCDIR}/libgcc/gthr-posix.h > ${.TARGET}

gthr-default.h: gthr-posix.h
	cp ${.ALLSRC} ${.TARGET}

ns_version=0
visibility=1
externtemplate=1
dualabi=1
cxx11abi=1
ldbl_compat='s,g,g,'
S1='s,define __GLIBCXX__,define __GLIBCXX__ ${GCCSHORTDATE},'
S2='s,define _GLIBCXX_INLINE_VERSION, define _GLIBCXX_INLINE_VERSION ${ns_version},'
S3='s,define _GLIBCXX_HAVE_ATTRIBUTE_VISIBILITY, define _GLIBCXX_HAVE_ATTRIBUTE_VISIBILITY ${visibility},'
S4='s,define _GLIBCXX_EXTERN_TEMPLATE$$, define _GLIBCXX_EXTERN_TEMPLATE ${externtemplate},'
S5='s,define _GLIBCXX_USE_DUAL_ABI, define _GLIBCXX_USE_DUAL_ABI ${dualabi},'
S6='s,define _GLIBCXX_USE_CXX11_ABI, define _GLIBCXX_USE_CXX11_ABI ${cxx11abi},'
S7='${ldbl_compat}'

c++config.h: config.h
	sed -e ${S1} -e ${S2} -e ${S3} -e ${S4} -e ${S5} -e ${S6} -e ${S7} \
	    < ${GCCDIR}/libstdc++-v3/include/bits/c++config > ${.TARGET}
	sed -e 's/HAVE_/_GLIBCXX_HAVE_/g' \
	    -e 's/PACKAGE/_GLIBCXX_PACKAGE/g' \
	    -e 's/VERSION/_GLIBCXX_VERSION/g' \
	    -e 's/WORDS_/_GLIBCXX_WORDS_/g' \
	    -e 's/ICONV_CONST/_GLIBCXX_ICONV_CONST/g' \
	    -e '/[ 	]_GLIBCXX_LONG_DOUBLE_COMPAT[ 	]/d' \
	    < ${.ALLSRC} >> ${.TARGET}
	echo "" >> ${.TARGET}
	echo "#endif // _GLIBCXX_CXX_CONFIG_H" >> ${.TARGET}

CLEANFILES=	gthr.h gthr-default.h gthr-single.h gthr-posix.h \
		c++config.h 

.include <bsd.lib.mk>
