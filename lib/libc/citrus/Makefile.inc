.PATH: ${.CURDIR}/../libc/citrus

.include "../../i18n_module/Makefile.shlib"

CFLAGS+=	-DI18NMODULE_MAJOR=${MODULE_SHLIB_MAJOR} -D_I18N_DYNAMIC
STATIC_LOCALES?=

SRCS+=	citrus_bcs.c citrus_csmapper.c citrus_ctype.c citrus_ctype_fallback.c
SRCS+=	citrus_db.c citrus_db_hash.c citrus_esdb.c citrus_hash.c
SRCS+=	citrus_iconv.c citrus_lookup.c citrus_mapper.c citrus_memstream.c
SRCS+=	citrus_mmap.c citrus_module.c citrus_none.c citrus_stdenc.c
SRCS+=  citrus_prop.c

# Add the locale modules to compile-in in the static libc.a
# (and profile/libc.a).
# If a new locale module is added, the includes and the table in
# citrus_module.c must be updated.
# A new module must have the file name citrus_xxx.c where xxx is the lower
# case name of the module.
# Currently the modules specified by STATIC_LOCALES are included in the
# shared libc (but never used). This is a bug.
.if ${STATIC_LOCALES} != ""
.PATH: ${.CURDIR}/../libc/citrus/modules
CFLAGS+=-I${.CURDIR}/../libc/citrus
CFLAGS+=-D_I18N_STATIC
.if defined(.PARSEDIR)
.for var in ${STATIC_LOCALES}
SRCS+= citrus_${var:tl}.c
CFLAGS+=-D_I18N_STATIC_${var}
.endfor
.else
# LEGACY MAKE - REMOVE FOR DFLY 3.6
.for var in ${STATIC_LOCALES}
SRCS+= citrus_${var:L}.c
CFLAGS+=-D_I18N_STATIC_${var}
.endfor
.endif
# iconv support
SRCS+=citrus_iconv_std.c citrus_mapper_std.c citrus_mapper_none.c citrus_mapper_serial.c citrus_utf1632.c
.endif # STATIC_LOCALES
