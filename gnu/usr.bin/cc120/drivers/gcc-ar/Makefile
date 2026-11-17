.include "../Makefile.inc"

PROG_CXX=	gcc-ar
NOMAN=		yes
SRCS=		gcc-ar.cc file-find.cc

FLAGS.gcc-ar.o=	${DRIVER_DEFINES}

FLAGS_GROUPS=	p
p_FLAGS_FILES=	gcc-ar.cc
p_FLAGS=	-DPERSONALITY=\"ar\"

.include <bsd.prog.mk>
