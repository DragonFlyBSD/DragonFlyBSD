# $DragonFly: src/lib/libc/citrus/Makefile.inc,v 1.1 2005/03/11 23:33:53 joerg Exp $

.PATH: ${.CURDIR}/../libc/citrus

.include "../../i18n_module/Makefile.shlib"

CFLAGS+=	-DI18NMODULE_MAJOR=${MODULE_SHLIB_MAJOR} -D_I18N_DYNAMIC

SRCS+=	citrus_bcs.c citrus_csmapper.c citrus_ctype.c citrus_ctype_fallback.c
SRCS+=	citrus_db.c citrus_db_hash.c citrus_esdb.c citrus_hash.c
SRCS+=	citrus_iconv.c citrus_lookup.c citrus_mapper.c citrus_memstream.c
SRCS+=	citrus_mmap.c citrus_module.c citrus_none.c citrus_stdenc.c
