.include "../scrnmaps/Makefile.scrnmaps"
srcdir=	${.CURDIR}/../scrnmaps

.for map in ${SCRMAPS:R}
PROGS+=	${map}

# Copy the source to apply different CFLAGS.
SRCS_${map}=	${map}.c
CLEANFILES+=	${map}.c
${map}.c: ${srcdir}/mkscrfil.c
	cp ${.ALLSRC} ${.TARGET}

CFLAGS_${map}=	-I${srcdir} -DFIL=\"${map}\"
.endfor

.include <bsd.hostprog.mk>
