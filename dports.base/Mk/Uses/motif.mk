# handle dependency on motif
#
# Feature:	motif
# Usage:	USES=motif
#
# If WANT_LESSTIF is defined in user make.conf then lesstif will be used
# instead of open-motif
#
# MAINTAINER: ports@FreeBSD.org

.if !defined(_INCLUDE_USES_MOTIF_MK)
_INCLUDE_USES_MOTIF_MK=	yes

.  if !empty(motif_ARGS)
IGNORE=	USES=motif takes no arguments
.  endif

.  if defined(WANT_LESSTIF)
LIB_DEPENDS+=		libXm.so:x11-toolkits/lesstif
.  elif defined(WANT_OPEN_MOTIF_DEVEL)
USE_XORG+=	xpm
LIB_DEPENDS+=		libXm.so.4:x11-toolkits/open-motif-devel
.  else
USE_XORG+=	xpm
LIB_DEPENDS+=		libXm.so.4:x11-toolkits/open-motif
.  endif

MOTIFLIB?=	-L${LOCALBASE}/lib -lXm
MAKE_ENV+=	MOTIFLIB="${MOTIFLIB}"

# We only need to include xorg.mk if we want USE_XORG modules
.  if defined(USE_XORG) && !empty(USE_XORG)
.include "${USESDIR}/xorg.mk"
.  endif

.endif
