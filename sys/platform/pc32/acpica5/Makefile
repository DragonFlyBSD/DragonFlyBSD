# $FreeBSD: src/sys/i386/acpica/Makefile,v 1.3.6.1 2003/08/22 20:49:21 jhb Exp $
# $DragonFly: src/sys/platform/pc32/acpica5/Makefile,v 1.1 2004/02/21 06:48:05 dillon Exp $
#

# Correct path for kernel builds
# Don't rely on the kernel's .depend file
.ifdef MAKESRCPATH
.PATH: ${MAKESRCPATH}
DEPENDFILE=
.else
MAKESRCPATH= ${.CURDIR}
CLEANFILES= acpi_wakecode.h acpi_wakecode.bin acpi_wakecode.o
.endif
CFLAGS+=	-I.

all: acpi_wakecode.h

acpi_wakecode.o: acpi_wakecode.S

acpi_wakecode.bin: acpi_wakecode.o
	objcopy -S -O binary acpi_wakecode.o acpi_wakecode.bin

acpi_wakecode.h: acpi_wakecode.bin acpi_wakecode.o
	sh ${MAKESRCPATH}/genwakecode.sh > acpi_wakecode.h

.include <bsd.prog.mk>

