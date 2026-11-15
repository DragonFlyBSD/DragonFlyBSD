.if !target(__<drivers.Makefile.inc>__)
__<drivers.Makefile.inc>__:

TOP_PREFIX=	../

# Extra defines for gcc.cc
FLAGS.gcc.o=	-DCONFIGURE_SPECS="\"\"" \
		-DACCEL_DIR_SUFFIX="\"\"" \
		-DDEFAULT_REAL_TARGET_MACHINE="\"${target_machine}\""

.include "../Makefile.inc"
.endif
