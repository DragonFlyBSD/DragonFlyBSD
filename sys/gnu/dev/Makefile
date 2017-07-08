.include "../../platform/${MACHINE_PLATFORM}/Makefile.inc"

SUBDIR=	misc

.for dir in ${SUBDIR}
.if empty(DEV_SUPPORT:M${dir}) && empty(DEV_SUPPORT:M${dir}/*)
SUBDIR:= ${SUBDIR:N${dir}}
.endif
.endfor

.include <bsd.subdir.mk>
