# $DragonFly: src/sys/dev/acpica5/Makefile.inc,v 1.6 2005/05/08 00:06:46 corecode Exp $

SYSACPICA_VERSION=	20050309
SYSACPICA_DIR?=		${SYSDIR}/contrib/dev/acpica-unix-${SYSACPICA_VERSION}
OSACPI_DIR?=		${SYSDIR}/dev/acpica5

CFLAGS+=	-I${.OBJDIR}			\
		-I${.CURDIR}			\
		-I${OSACPI_DIR}			\
		-I${SYSACPICA_DIR}/include

.PATH:	${SYSACPICA_DIR}/interpreter/dispatcher	\
	${SYSACPICA_DIR}/interpreter/executer	\
	${SYSACPICA_DIR}/interpreter/parser	\
	${SYSACPICA_DIR}/events			\
	${SYSACPICA_DIR}/hardware		\
	${SYSACPICA_DIR}/namespace		\
	${SYSACPICA_DIR}/resources		\
	${SYSACPICA_DIR}/tables			\
	${SYSACPICA_DIR}/utilities		\
	${SYSACPICA_DIR}/debugger		\
	${SYSACPICA_DIR}/disassembler		\
	${SYSACPICA_DIR}/INTERPRETER/DISPATCHER	\
	${SYSACPICA_DIR}/INTERPRETER/EXECUTER	\
	${SYSACPICA_DIR}/INTERPRETER/PARSER	\
	${SYSACPICA_DIR}/EVENTS			\
	${SYSACPICA_DIR}/HARDWARE		\
	${SYSACPICA_DIR}/NAMESPACE		\
	${SYSACPICA_DIR}/RESOURCES		\
	${SYSACPICA_DIR}/TABLES			\
	${SYSACPICA_DIR}/UTILITIES

# acpi.h includes "platform/acenv.h".  This is the easiest way to create
# a modified acenv.h
#
${.OBJDIR}/acpi.h: ${SYSACPICA_DIR}/include/acpi.h
	cp ${.ALLSRC} ${.TARGET}

${.OBJDIR}/platform/acenv.h: ${SYSACPICA_DIR}/include/platform/acenv.h
	if [ ! -d ${.OBJDIR}/platform ]; then mkdir ${.OBJDIR}/platform; fi
	cat ${.ALLSRC} | \
	sed -e 's/__FreeBSD__/__DragonFly__/' | \
	sed -e 's/acfreebsd.h/acdragonfly.h/' > ${.TARGET}.new
	mv -f ${.TARGET}.new ${.TARGET}

SRCS+= ${.OBJDIR}/acpi.h ${.OBJDIR}/platform/acenv.h

CLEANFILES+=	${.OBJDIR}/acpi.h
CLEANDIRS+=	${.OBJDIR}/platform
