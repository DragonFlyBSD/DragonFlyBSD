#
# acpibin - Binary ACPI table utility
#

UTILDIR=${.CURDIR}/../../../sys/contrib/dev/acpica/source/tools/acpibin

.PATH:	${UTILDIR}

PROG=	acpibin
MAN=	acpibin.8

SRCS=	\
	abcompare.c \
	abmain.c

SRCS+=	\
	cmfsize.c \
	getopt.c \
	utalloc.c \
	utbuffer.c \
	utcache.c \
	utdecode.c \
	utexcep.c \
	utglobal.c \
	utlock.c \
	utmath.c \
	utmisc.c \
	utmutex.c \
	utstate.c \
	utstring.c \
	utxferror.c \
	osunixxf.c

CFLAGS+= \
	-DACPI_BIN_APP \
	-I${UTILDIR}

.include <bsd.prog.mk>
