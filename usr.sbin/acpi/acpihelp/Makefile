#
# acpihelp - ACPI Help utility. Displays ASL operator syntax and
# information about ACPI predefined names.
#

UTILDIR=${.CURDIR}/../../../sys/contrib/dev/acpica/source/tools/acpihelp

.PATH:	${UTILDIR}

PROG=	acpihelp
MAN=	acpihelp.8

SRCS=	\
	ahamlops.c \
	ahaslkey.c \
	ahaslops.c \
	ahdecode.c \
	ahpredef.c \
	ahmain.c

SRCS+=	\
	getopt.c \
	utexcep.c \
	utpredef.c

CFLAGS+= \
	-DACPI_HELP_APP \
	-I${UTILDIR}

.include <bsd.prog.mk>
