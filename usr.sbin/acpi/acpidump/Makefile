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
	osunixdir.c \
	osunixmap.c \
	tbprint.c \
	tbxfroot.c \
	utbuffer.c \
	utexcep.c \
	utmath.c \
	utstring.c \
	utxferror.c \
	getopt.c

SRCS+=	\
	osdragonflytbl.c

CFLAGS+= \
	-DACPI_DUMP_APP \
	-I${UTILDIR} \
	-D_FreeBSD

.include <bsd.prog.mk>
