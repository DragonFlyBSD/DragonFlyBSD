.include "../Makefile.inc"

PROG_CXX=	gcc-ranlib
NOMAN=		yes
SRCS=		gcc-ranlib.cc file-find.cc

FLAGS.gcc-ranlib.o=	${DRIVER_DEFINES}

FLAGS_GROUPS=	p
p_FLAGS_FILES=	gcc-ranlib.cc
p_FLAGS=	-DPERSONALITY=\"ranlib\"

CLEANFILES+=	gcc-ranlib.cc
gcc-ranlib.cc: ${GCCDIR}/gcc/gcc-ar.cc
	cp ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
