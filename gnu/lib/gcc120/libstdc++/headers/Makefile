RELUP=	/..
.include "../Makefile.inc"
SRCDIR=	${GCCDIR}/libstdc++-v3
.include "Makefile.headers"

.PATH:	${SRCDIR}

HD=	${INCLUDEDIR}/c++/${GCCPOINTVER}

# These sections are detailed in Makefile.headers.
HSECT=	std bits pstl backward ext ext_compat tr1 tr2 decimal \
	experimental experimental_bits \
	experimental_filesystem experimental_bits_filesystem \
	c_base c_compatibility debug parallel
HSECT+=	sup bits_sup
.for i in 1 2 3 4 5 6 7
PBHDRS+=${pb_headers${i}}
.endfor

# The libstdc++ components and products have been updated to use '-nostdinc++'
# and thus don't require these headers be installed before building them, so
# use FILESGROUPS instead of INCSGROUPS to skip running "buildincludes" and
# "installincludes" before "depend".

.for i in ${HSECT}
FILESGROUPS+=		${i}_headers
${i}_headersDIR=	${HD}/${${i}_builddir}
.endfor

.for i in ${pb_builddir} ${pb_subdirs}
FILESGROUPS+=		_${i}_headers
_${i}_headersDIR=	${HD}/${i}
. for f in ${PBHDRS}
.  if ${i:S/${pb_builddir}//} == ${f:H:S/${pb_srcdir}//}
_${i}_headers+=		${f}
.  endif
. endfor
.endfor

.for h in bits_host_headers host_headers host_headers_extra thread_host_headers
FILESGROUPS+=		${h}
${h}DIR=		${HD}/bits
.endfor
FILESGROUPS+=		ext_host_headers
ext_host_headersDIR=	${HD}/ext

# Copy the ${host_headers} and ${bits_host_headers} to 'bits' directory as
# they are required by the generated ${host_headers_extra} and the
# convenience libraries.
# Also copy the ${bits_sup_headers} to 'bits' directory as they are required
# by the ${sup_headers}.
copy_headers: mkdirs
.for f in ${host_headers} ${bits_host_headers} ${bits_sup_headers}
copy_headers: bits/${f:T}
bits/${f:T}: ${f}
	cp ${.ALLSRC} ${.TARGET}
.endfor

# This header is not installed, it's only used to build libstdc++ itself.
GENFILES+=	bits/largefile-config.h
bits/largefile-config.h: config.h
	-grep 'define _DARWIN_USE_64_BIT_INODE' ${.ALLSRC}	>  ${.TARGET}
	-grep 'define _FILE_OFFSET_BITS' ${.ALLSRC}		>> ${.TARGET}
	-grep 'define _LARGE_FILES' ${.ALLSRC}			>> ${.TARGET}

CLEANDIRS+=	bits
mkdirs:
	mkdir -p bits

CLEANFILES+=	${GENFILES}

depend all: mkdirs copy_headers ${GENFILES}
.ORDER: mkdirs copy_headers
.ORDER: mkdirs ${GENFILES}

.include <bsd.lib.mk>
