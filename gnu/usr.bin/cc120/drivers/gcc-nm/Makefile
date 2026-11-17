.include "../Makefile.inc"

PROG_CXX=	gcc-nm
NOMAN=		yes
SRCS=		gcc-nm.cc file-find.cc

FLAGS.gcc-nm.o=	${DRIVER_DEFINES}

FLAGS_GROUPS=	p
p_FLAGS_FILES=	gcc-nm.cc
p_FLAGS=	-DPERSONALITY=\"nm\"

CLEANFILES+=	gcc-nm.cc
gcc-nm.cc: ${GCCDIR}/gcc/gcc-ar.cc
	cp ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
