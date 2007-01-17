# $DragonFly: src/sys/conf/acpi.mk,v 1.5 2007/01/17 17:31:19 y0netan1 Exp $
#

ACPICA_VERSION=		20061109
ACPICA_DIR?=		contrib/dev/acpica-unix-${ACPICA_VERSION}
OSACPI_MI_DIR?=		dev/acpica5
OSACPI_MD_DIR?=		machine/${MACHINE}/acpica5

.if !defined(SYSDIR) && defined(S)
SYSDIR=	$S
.endif

ACPICA_KERN_PATHS = \
	${SYSDIR}/${ACPICA_DIR}/interpreter/dispatcher 	\
	${SYSDIR}/${ACPICA_DIR}/interpreter/executer	\
	${SYSDIR}/${ACPICA_DIR}/interpreter/parser	\
	${SYSDIR}/${ACPICA_DIR}/events			\
	${SYSDIR}/${ACPICA_DIR}/hardware		\
	${SYSDIR}/${ACPICA_DIR}/namespace		\
	${SYSDIR}/${ACPICA_DIR}/resources		\
	${SYSDIR}/${ACPICA_DIR}/tables			\
	${SYSDIR}/${ACPICA_DIR}/utilities		\
	${SYSDIR}/${ACPICA_DIR}/debugger		\
	${SYSDIR}/${ACPICA_DIR}/disassembler

ACPICA_UTIL_PATHS = \
	${ACPICA_KERN_PATHS}				\
	${SYSDIR}/${ACPICA_DIR}				\
	${SYSDIR}/${ACPICA_DIR}/compiler		\
	${SYSDIR}/${ACPICA_DIR}/common

${.OBJDIR}/acpi.h: ${SYSDIR}/${ACPICA_DIR}/include/acpi.h
	cp ${.ALLSRC} ${.TARGET}

${.OBJDIR}/platform/acenv.h: ${SYSDIR}/${ACPICA_DIR}/include/platform/acenv.h
	mkdir -p ${.OBJDIR}/platform
	sed -e 's/__FreeBSD__/__DragonFly__/' \
	    -e 's/acfreebsd.h/acdragonfly.h/' ${.ALLSRC} > ${.TARGET}.new
	mv -f ${.TARGET}.new ${.TARGET}
