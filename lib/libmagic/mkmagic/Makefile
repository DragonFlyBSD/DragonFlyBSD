CONTRIBDIR=	${.CURDIR}/../../../contrib/file/src
.PATH:	${CONTRIBDIR}

PROG=	mkmagic
SRCS=	apprentice.c encoding.c cdf_time.c funcs.c magic.c print.c

MAGICSHAREDIR=${SHAREDIR}/misc
MAGIC=	${MAGICSHAREDIR}/magic

CFLAGS+=	-DHAVE_CONFIG_H -DCOMPILE_ONLY -DMAGIC=\"${MAGIC}\"
CFLAGS+=	-I${.CURDIR}/../libmagic

# XXX -Wunused-function in apprentice.c
WARNS= 1

build-tools: mkmagic.nx

# this mkmagic is used in place, it is not installed anywhere
install:

.include <bsd.hostprog.mk>
