# Handle dependencies on Blas / Lapack
#
# Feature:	blaslapack
# Usage:	USES=blaslapack or USES=blaslapack:ARGS
# Valid ARGS:	atlas gotoblas netlib (default) openblas
#
# Provides:	BLASLIB and LAPACKLIB
#
# Maintainer:	thierry@FreeBSD.org

.if !defined(_INCLUDE_USES_BLASLAPACK_MK)
_INCLUDE_USES_BLASLAPACK_MK=	yes

_valid_ARGS=	atlas gotoblas netlib openblas

_DEFAULT_BLASLAPACK=	netlib

.  if empty(blaslapack_ARGS)
blaslapack_ARGS=	${_DEFAULT_BLASLAPACK}
.  endif

LDFLAGS+=	-L${LOCALBASE}/lib

.  if ${blaslapack_ARGS} == atlas
LIB_DEPENDS+=	libatlas.so:math/atlas
_BLASLIB=	ptf77blas
LAPACKLIB=	-lalapack -lptcblas
_ATLASLIB=	atlas
ATLASLIB=	-l${_ATLASLIB}
BLA_VENDOR=	ATLAS
.  elif ${blaslapack_ARGS} == gotoblas
LIB_DEPENDS+=	libgoto2.so:math/gotoblas
LIB_DEPENDS+=	liblapack.so:math/lapack
_BLASLIB=	goto2p
LAPACKLIB=	-lgoto2p
BLA_VENDOR=	Goto
.  elif ${blaslapack_ARGS} == netlib
LIB_DEPENDS+=	libblas.so:math/blas
LIB_DEPENDS+=	liblapack.so:math/lapack
_BLASLIB=	blas
LAPACKLIB=	-llapack
BLA_VENDOR=	Generic
.  elif ${blaslapack_ARGS} == openblas
LIB_DEPENDS+=	libopenblas.so:math/openblas
_BLASLIB=	openblas
LAPACKLIB=	-lopenblas
BLA_VENDOR=	OpenBLAS
.  else
IGNORE=		USES=blaslapack: invalid arguments: ${blaslapack_ARGS}
.  endif

BLASLIB=	-l${_BLASLIB}

.  if ${USES:Mcmake} || ${USES:Mcmake\:*}
CONFIGURE_ENV+=	BLA_VENDOR="${BLA_VENDOR}"
.  endif

.endif
