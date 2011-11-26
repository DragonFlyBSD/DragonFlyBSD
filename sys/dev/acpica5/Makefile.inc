# $DragonFly: src/sys/dev/acpica5/Makefile.inc,v 1.8 2007/01/17 17:31:19 y0netan1 Exp $

.include "${SYSDIR}/conf/acpi.mk"

CFLAGS+=	-I${.OBJDIR}			\
		-I${.OBJDIR}/include		\
		-I${.CURDIR}			\
		-I${SYSDIR}/${OSACPI_MI_DIR}	\
		-I${SYSDIR}/${ACPICA_DIR}/include

SRCS+= ${.OBJDIR}/acpi.h ${.OBJDIR}/platform/acenv.h

CLEANFILES+=	${.OBJDIR}/acpi.h
CLEANDIRS+=	${.OBJDIR}/platform
