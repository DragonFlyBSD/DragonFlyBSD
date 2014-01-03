#
# acpisrc - ACPICA source code conversion utility
#

UTILDIR=${.CURDIR}/../../../sys/contrib/dev/acpica/source/tools/acpisrc

.PATH:	${UTILDIR}

PROG=	acpisrc
NOMAN=

SRCS=	\
	ascase.c \
	asconvrt.c \
	asfile.c \
	asmain.c \
	asremove.c \
	astable.c \
	asutils.c

SRCS+=	\
	osunixdir.c \
	getopt.c

CFLAGS+= \
	-DACPI_SRC_APP \
	-I${UTILDIR}

.include <bsd.prog.mk>
