.include "../../Makefile.inc1"
.include "../Makefile.gld"

PROG_CXX=	incremental-dump
OBJS=		incremental-dump.o
COMMOBJS=	${GOLD_COMMON:S|^|${.OBJDIR:H}/gold-linker/|:.cc=.o}
CFLAGS+=	-I${.CURDIR}/../../block2/libgold
LDADD=		${STD_LDADD}
NOMAN=

# The <bsd.obj.mk> prevents searching for object files in other
# directory's work area.  In order to avoid building the COMMONOBJS
# twice, a custom target has to be applied, and the sources have to
# defined as OBJS, not SRCS for this to work.

incremental-dump: ${OBJS} ${COMMOBJS}
	${CXX_LINK} ${CXXFLAGS} ${LDFLAGS} -o ${.TARGET} \
		${OBJS} ${COMMOBJS} ${LDADD}

.include <bsd.prog.mk>
