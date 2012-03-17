.include "${.CURDIR}/../../platform/${MACHINE_PLATFORM}/Makefile.inc"

SUBDIR=	vkernel

.for dir in ${SUBDIR}
.if empty(DEV_SUPPORT:Mvirtual) && \
	empty(DEV_SUPPORT:Mvirtual/${dir}) && empty(DEV_SUPPORT:Mvirtual/${dir}/*)
SUBDIR:= ${SUBDIR:N${dir}}
.endif
.endfor

.include <bsd.subdir.mk>
