CONTRIBDIR=	${.CURDIR}/../../../contrib/file/src
.PATH:	${CONTRIBDIR}

PROG=	mkmagic
INCS=	magic.h
SRCS=	apprentice.c encoding.c cdf_time.c funcs.c magic.c print.c ${INCS}

MAGICSHAREDIR=${SHAREDIR}/misc
MAGIC=	${MAGICSHAREDIR}/magic

CFLAGS+=	-DHAVE_CONFIG_H -DCOMPILE_ONLY -DMAGIC=\"${MAGIC}\"
CFLAGS+=	-I${.CURDIR}/../libmagic

# needed to access magic.h
#
CFLAGS+=	-I${.OBJDIR}

VER!=  grep -w VERSION ${.CURDIR}/../libmagic/config.h | awk -F\" '{print $$2}'

# XXX -Wunused-function in apprentice.c
WARNS= 1

build-tools: mkmagic.nx

# this mkmagic is used in place, it is not installed anywhere
install:

.for inc in ${INCS}
${inc}: ${inc}.in
	sed -e 's,X.YY,${VER:S,",,g:S,.,,g},g' ${.ALLSRC} > ${.TARGET}
.endfor

.include <bsd.hostprog.mk>
