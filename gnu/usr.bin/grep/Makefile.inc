BASEDIR=	${.CURDIR}/${RELATIVE}../../../../contrib/grep

.if defined(BOOTSTRAPPING)
CFLAGS+=	-DBOOTSTRAPPING
# Helper for bootstrapping over C++11 features.
. if ${WORLD_VERSION} <= 400103
CFLAGS+=	-Dmax_align_t="long double"
. endif
.endif

CSTD=	c11
