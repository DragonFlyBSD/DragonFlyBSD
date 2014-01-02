.include "${SYSDIR}/conf/acpi.mk"

CFLAGS+=	-I${.OBJDIR}			\
		-I${.OBJDIR}/include		\
		-I${.CURDIR}			\
		-I${SYSDIR}/${OSACPI_MI_DIR}	\
		-I${SYSDIR}/${ACPICA_DIR}/include
