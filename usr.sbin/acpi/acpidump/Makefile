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
	osunixdir.c \
	osunixmap.c \
	osunixxf.c \
	tbprint.c \
	tbxfroot.c \
	utascii.c \
	utbuffer.c \
	utdebug.c \
	utexcep.c \
	utglobal.c \
	uthex.c \
	utmath.c \
	utnonansi.c \
	utstring.c \
	utstrtoul64.c \
	utxferror.c

SRCS+=	\
	osbsdtbl.c

CFLAGS+= \
	-DACPI_DUMP_APP \
	-I${UTILDIR} \
	-D_DragonFly

.include <bsd.prog.mk>
