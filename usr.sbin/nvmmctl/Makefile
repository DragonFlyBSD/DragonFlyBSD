PROG=	nvmmctl
MAN=	nvmmctl.8

BINGRP=	nvmm
BINMODE=2555

LDADD+=	-lutil -lnvmm
DPADD+=	${LIBUTIL} ${LIBNVMM}

.if ${WORLD_VERSION} < 600102 # host has no nvmm group yet
INSTALLFLAGS=-N ${.CURDIR}/../../etc
.endif

.include <bsd.prog.mk>
