BASEDIR=	${.CURDIR}/${RELATIVE}../../../../contrib/grep

.if defined(BOOTSTRAPPING)
CFLAGS+=	-DBOOTSTRAPPING
.endif
