# $Id: Makefile.inc,v 1.1 2000/07/14 18:16:22 iwasaki Exp $
# $FreeBSD: src/usr.sbin/acpi/Makefile.inc,v 1.8 2003/08/07 16:51:50 njl Exp $
# $DragonFly: src/usr.sbin/acpi/Makefile.inc,v 1.2 2005/03/11 18:27:49 y0netan1 Exp $

SYSDIR?= ${.CURDIR}/../../../sys
OSACPI_DIR= ${SYSDIR}/dev/acpica5
.include "${OSACPI_DIR}/Makefile.inc"

.PATH:		${SYSACPICA_DIR}		\
		${SYSACPICA_DIR}/compiler	\
		${SYSACPICA_DIR}/common

CFLAGS+=	-I${SYSACPICA_DIR}/compiler

.if exists(${.CURDIR}/../../Makefile.inc)
.include "${.CURDIR}/../../Makefile.inc"
.endif
