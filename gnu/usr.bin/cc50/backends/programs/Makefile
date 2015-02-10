# These backends can be built in parallel

SUBDIR_ORDERED=		# maximum parallelism

SUBDIR+=	cc1
SUBDIR+=	cc1plus
SUBDIR+=	cc1obj
SUBDIR+=	lto1

.if ! defined (BOOTSTRAPPING)
SUBDIR+=	lto-wrapper
.endif

.include <bsd.subdir.mk>
