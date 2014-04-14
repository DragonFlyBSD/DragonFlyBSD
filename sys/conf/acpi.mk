ACPICA_DIR?=		contrib/dev/acpica/source
ACPICA_COMP_DIR?=	contrib/dev/acpica/source/components
OSACPI_MI_DIR?=		dev/acpica
OSACPI_MD_DIR?=		platform/${MACHINE_PLATFORM}/acpica

.if !defined(SYSDIR) && defined(S)
SYSDIR=	$S
.endif

ACPICA_KERN_PATHS = \
	${SYSDIR}/${ACPICA_DIR}/common			\
	${SYSDIR}/${ACPICA_COMP_DIR}/dispatcher 	\
	${SYSDIR}/${ACPICA_COMP_DIR}/executer		\
	${SYSDIR}/${ACPICA_COMP_DIR}/parser		\
	${SYSDIR}/${ACPICA_COMP_DIR}/events		\
	${SYSDIR}/${ACPICA_COMP_DIR}/hardware		\
	${SYSDIR}/${ACPICA_COMP_DIR}/namespace		\
	${SYSDIR}/${ACPICA_COMP_DIR}/resources		\
	${SYSDIR}/${ACPICA_COMP_DIR}/tables		\
	${SYSDIR}/${ACPICA_COMP_DIR}/utilities		\
	${SYSDIR}/${ACPICA_COMP_DIR}/debugger		\
	${SYSDIR}/${ACPICA_COMP_DIR}/disassembler

ACPICA_UTIL_PATHS = \
	${SYSDIR}/${ACPICA_DIR}/common			\
	${SYSDIR}/${ACPICA_DIR}/os_specific/service_layers \
	${SYSDIR}/${ACPICA_COMP_DIR}/debugger		\
	${SYSDIR}/${ACPICA_COMP_DIR}/disassembler	\
	${SYSDIR}/${ACPICA_COMP_DIR}/dispatcher		\
	${SYSDIR}/${ACPICA_COMP_DIR}/events		\
	${SYSDIR}/${ACPICA_COMP_DIR}/executer		\
	${SYSDIR}/${ACPICA_COMP_DIR}/hardware		\
	${SYSDIR}/${ACPICA_COMP_DIR}/namespace		\
	${SYSDIR}/${ACPICA_COMP_DIR}/parser		\
	${SYSDIR}/${ACPICA_COMP_DIR}/resources		\
	${SYSDIR}/${ACPICA_COMP_DIR}/tables		\
	${SYSDIR}/${ACPICA_COMP_DIR}/utilities
