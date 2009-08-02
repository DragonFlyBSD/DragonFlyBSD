.include "../Makefile.inc"

INCS=		\
	exception new typeinfo cxxabi.h cxxabi-forced.h exception_defines.h \
	initializer_list exception_ptr.h

INCSDIR=	${INCLUDEDIR}/c++/${GCCPOINTVER}

LIB=		supc++

.include "Makefile.src"

.PATH: ${GCCDIR}/libiberty

.include <bsd.lib.mk>
