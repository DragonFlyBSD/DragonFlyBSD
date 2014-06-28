#
# acpixtract - extract binary ACPI tables from acpidump text output
#

UTILDIR=${.CURDIR}/../../../sys/contrib/dev/acpica/source/tools/acpixtract

.PATH:	${UTILDIR}

PROG=	acpixtract
MAN=	acpixtract.8

SRCS=	\
	acpixtract.c \
	axmain.c

SRCS+=	\
	getopt.c \
	oslibcfs.c \
	osunixxf.c \
	utdebug.c \
	utexcep.c \
	utglobal.c \
	utmath.c \
	utprint.c \
	utxferror.c

CFLAGS+= \
	-DACPI_XTRACT_APP \
	-I${UTILDIR}

.include <bsd.prog.mk>
