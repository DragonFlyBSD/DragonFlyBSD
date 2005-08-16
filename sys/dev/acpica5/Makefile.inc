# $DragonFly: src/sys/dev/acpica5/Makefile.inc,v 1.7 2005/08/16 10:31:35 y0netan1 Exp $

.include "${SYSDIR}/conf/acpi.mk"

CFLAGS+=	-I${.OBJDIR}			\
		-I${.CURDIR}			\
		-I${SYSDIR}/${OSACPI_MI_DIR}	\
		-I${SYSDIR}/${ACPICA_DIR}/include

SRCS+= ${.OBJDIR}/acpi.h ${.OBJDIR}/platform/acenv.h

CLEANFILES+=	${.OBJDIR}/acpi.h
CLEANDIRS+=	${.OBJDIR}/platform
