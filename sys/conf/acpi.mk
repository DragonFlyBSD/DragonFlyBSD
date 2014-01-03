ACPICA_DIR?=		contrib/dev/acpica/source
OSACPI_MI_DIR?=		dev/acpica
OSACPI_MD_DIR?=		platform/${MACHINE_PLATFORM}/acpica

.if !defined(SYSDIR) && defined(S)
SYSDIR=	$S
.endif

ACPICA_KERN_PATHS = \
	${SYSDIR}/${ACPICA_DIR}/dispatcher 		\
	${SYSDIR}/${ACPICA_DIR}/executer		\
	${SYSDIR}/${ACPICA_DIR}/parser			\
	${SYSDIR}/${ACPICA_DIR}/events			\
	${SYSDIR}/${ACPICA_DIR}/hardware		\
	${SYSDIR}/${ACPICA_DIR}/namespace		\
	${SYSDIR}/${ACPICA_DIR}/resources		\
	${SYSDIR}/${ACPICA_DIR}/tables			\
	${SYSDIR}/${ACPICA_DIR}/utilities		\
	${SYSDIR}/${ACPICA_DIR}/debugger		\
	${SYSDIR}/${ACPICA_DIR}/disassembler

ACPICA_UTIL_PATHS = \
	${SYSDIR}/${ACPICA_DIR}/common			\
	${SYSDIR}/${ACPICA_DIR}/components/debugger	\
	${SYSDIR}/${ACPICA_DIR}/components/disassembler	\
	${SYSDIR}/${ACPICA_DIR}/components/dispatcher	\
	${SYSDIR}/${ACPICA_DIR}/components/events	\
	${SYSDIR}/${ACPICA_DIR}/components/executer	\
	${SYSDIR}/${ACPICA_DIR}/components/hardware	\
	${SYSDIR}/${ACPICA_DIR}/components/namespace	\
	${SYSDIR}/${ACPICA_DIR}/components/parser	\
	${SYSDIR}/${ACPICA_DIR}/components/resources	\
	${SYSDIR}/${ACPICA_DIR}/components/tables	\
	${SYSDIR}/${ACPICA_DIR}/components/utilities	\
	${SYSDIR}/${ACPICA_DIR}/os_specific/service_layers

${.OBJDIR}/acpi.h: ${SYSDIR}/${ACPICA_DIR}/include/acpi.h
	cp ${.ALLSRC} ${.TARGET}

${.OBJDIR}/platform/acenv.h: ${SYSDIR}/${ACPICA_DIR}/include/platform/acenv.h
	mkdir -p ${.OBJDIR}/platform
	sed -e 's/__FreeBSD__/__DragonFly__/' \
	    -e 's/acfreebsd.h/acdragonfly.h/' ${.ALLSRC} > ${.TARGET}.new
	mv -f ${.TARGET}.new ${.TARGET}
