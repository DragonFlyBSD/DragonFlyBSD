# hostprog helpers for screen maps generation
.PATH: ${.CURDIR}/../scrnmaps

# SCRMAPS
.include "./Makefile.scrnmaps"

SCRMAPS_MK=	${SCRMAPS:R:S/$/.nx/g}

${SCRMAPS_MK}: ${.TARGET:R} mkscrfil.c
	${NXCC} ${NXCFLAGS} -I${.CURDIR}/../scrnmaps -DFIL=\"${.TARGET:R}\" ${NXLDFLAGS} \
		-o ${.TARGET} ${.CURDIR}/../scrnmaps/mkscrfil.c

CLEANFILES+=	${SCRMAPS_MK}

genfiles: ${SCRMAPS_MK}
depend all: genfiles

build-tools: ${SCRMAPS_MK}

# this tools are used in place, they are not installed anywhere
install:

# Hint HOST_CCVER handling.
__USE_HOST_CCVER=

.include <bsd.obj.mk>
.include <bsd.dep.mk>
.include <bsd.sys.mk>
