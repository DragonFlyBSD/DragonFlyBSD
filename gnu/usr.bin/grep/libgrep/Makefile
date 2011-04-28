.include "../Makefile.inc"

LIB=		grep
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/src
.PATH:		${CONTRIBDIR}

CFLAGS+=	-I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-I${BASEDIR}/lib -I${.CURDIR}/../libgreputils
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		kwset.c \
		dfa.c \
		searchutils.c \
		dfasearch.c \
		kwsearch.c \
		pcresearch.c \
		main.c

.include <bsd.lib.mk>
