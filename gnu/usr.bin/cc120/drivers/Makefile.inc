.if !target(__<drivers.Makefile.inc>__)
__<drivers.Makefile.inc>__:

TOP_PREFIX=	../

# Extra defines for gcc.cc
FLAGS.gcc.o=	${DRIVER_DEFINES}

.include "../Makefile.inc"
.endif
