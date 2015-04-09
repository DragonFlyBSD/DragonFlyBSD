#
# acpidump - ACPI table dump utility (binary to ascii hex)
#

UTILDIR=${.CURDIR}/../../../sys/contrib/dev/acpica/source/tools/acpidump

.PATH:	${UTILDIR}

PROG=	acpidump
MAN=	acpidump.8

SRCS=	\
	apdump.c \
	apfiles.c \
	apmain.c

SRCS+=	\
	cmfsize.c \
	getopt.c \
	oslibcfs.c \
	osunixdir.c \
	osunixmap.c \
	osunixxf.c \
	tbprint.c \
	tbxfroot.c \
	utbuffer.c \
	utdebug.c \
	utexcep.c \
	utglobal.c \
	utmath.c \
	utprint.c \
	utstring.c \
	utxferror.c

SRCS+=	\
	osbsdtbl.c

CFLAGS+= \
	-DACPI_DUMP_APP \
	-I${UTILDIR}

.include <bsd.prog.mk>
