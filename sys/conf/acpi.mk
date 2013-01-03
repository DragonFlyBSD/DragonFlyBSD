ACPICA_DIR?=		contrib/dev/acpica-unix
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
