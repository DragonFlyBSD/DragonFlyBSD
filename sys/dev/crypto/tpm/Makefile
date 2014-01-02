# $FreeBSD: src/sys/modules/tpm/Makefile,v 1.2 2010/08/13 04:00:35 takawata Exp $

KMOD=	tpm

SRCS=	tpm.c bus_if.h device_if.h
SRCS+=	tpm_isa.c tpm_acpi.c isa_if.h opt_acpi.h acpi_if.h

CFLAGS+=-I${.CURDIR}/../../acpica
CFLAGS+=-I${.CURDIR}/../../../contrib/dev/acpica/source/include

.include <bsd.kmod.mk>
