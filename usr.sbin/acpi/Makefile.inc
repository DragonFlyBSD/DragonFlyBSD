# $Id: Makefile.inc,v 1.1 2000/07/14 18:16:22 iwasaki Exp $
# $FreeBSD: src/usr.sbin/acpi/Makefile.inc,v 1.8 2003/08/07 16:51:50 njl Exp $
# $DragonFly: src/usr.sbin/acpi/Makefile.inc,v 1.1 2004/07/05 00:22:40 dillon Exp $

# XXX use /sys/dev/acpica5/Makefile.inc
SYSDIR?= ${.CURDIR}/../../../sys
OSACPI_DIR= ${SYSDIR}/dev/acpica5
.include "${OSACPI_DIR}/Makefile.inc"
ACPICA_DIR?=	${SYSDIR}/${SYSACPICA}

CFLAGS+= -I${.OBJDIR}		\
	-I${SYSDIR}		\
	-I${OSACPI_DIR}		\
	-I${ACPICA_DIR}/include \
	-I${ACPICA_DIR}/compiler

.if exists(${.CURDIR}/../../Makefile.inc)
.include "${.CURDIR}/../../Makefile.inc"
.endif

# XXX share this with /sys/dev/acpica5/Makefile.inc?
.PATH:	${ACPICA_DIR}				\
	${ACPICA_DIR}/compiler			\
	${ACPICA_DIR}/common			\
	${ACPICA_DIR}/debugger			\
	${ACPICA_DIR}/disassembler		\
	${ACPICA_DIR}/events			\
	${ACPICA_DIR}/include			\
	${ACPICA_DIR}/hardware			\
	${ACPICA_DIR}/interpreter/dispatcher	\
	${ACPICA_DIR}/interpreter/executer	\
	${ACPICA_DIR}/interpreter/parser	\
	${ACPICA_DIR}/namespace			\
	${ACPICA_DIR}/resources			\
	${ACPICA_DIR}/tables			\
	${ACPICA_DIR}/utilities			\
	${ACPICA_DIR}/EVENTS			\
	${ACPICA_DIR}/HARDWARE			\
	${ACPICA_DIR}/INTERPRETER/DISPATCHER	\
	${ACPICA_DIR}/INTERPRETER/EXECUTER	\
	${ACPICA_DIR}/INTERPRETER/PARSER	\
	${ACPICA_DIR}/NAMESPACE			\
	${ACPICA_DIR}/TABLES			\

# acpi.h includes "platform/acenv.h".  This is the easiest way to create
# a modified acenv.h
#
${.OBJDIR}/acpi.h: ${ACPICA_DIR}/include/acpi.h
	cp ${.ALLSRC} ${.TARGET}

${.OBJDIR}/platform/acenv.h: ${ACPICA_DIR}/include/platform/acenv.h
	if [ ! -d ${.OBJDIR}/platform ]; then mkdir ${.OBJDIR}/platform; fi
	cat ${.ALLSRC} | \
	sed -e 's/__FreeBSD__/__DragonFly__/' | \
	sed -e 's/acfreebsd.h/acdragonfly.h/' > ${.TARGET}.new
	mv -f ${.TARGET}.new ${.TARGET}

SRCS+= ${.OBJDIR}/acpi.h ${.OBJDIR}/platform/acenv.h
