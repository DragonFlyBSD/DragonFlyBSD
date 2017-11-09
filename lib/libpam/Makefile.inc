
# only used by static_libpam and libpam_*.a
STATIC_CFLAGS+=	-DOPENPAM_STATIC_MODULES

# Workaround a diff between security/openpam.h and openpam_static.c
STATIC_CFLAGS+= -D_openpam_static_modules=openpam_static_modules

.include "../Makefile.inc"
