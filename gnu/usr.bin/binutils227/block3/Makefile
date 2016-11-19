# Build in serial
# incremental dump uses all the same object files as the gold linker
# except for one, so we reuse them instead of building them twice
SUBDIR=	gold-linker incremental-dump

SUBDIR_ORDERED= ${SUBDIR}

.include <bsd.subdir.mk>
