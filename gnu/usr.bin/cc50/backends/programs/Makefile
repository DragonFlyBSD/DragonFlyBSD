# These backends can be built in parallel

SUBDIR_ORDERED=		# maximum parallelism

SUBDIR+=	cc1
SUBDIR+=	cc1plus
SUBDIR+=	cc1obj
SUBDIR+=	collect2
SUBDIR+=	lto1

.if ! defined (BOOTSTRAPPING) || ${CFLAGS:M-flto}
SUBDIR+=	lto-wrapper
.endif

.include <bsd.subdir.mk>
