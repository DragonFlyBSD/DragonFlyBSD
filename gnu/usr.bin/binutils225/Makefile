# Binutils 2.25 builds 3 blocks in series
SUBDIR=	block1 block2

# Don't build gold linker during stage 3 cross-tools unless
# WORLD_LDVER has been set to ld.gold.  This same check must be included
# in block 2 where libgold and ld.bfd are built.
.if !defined(SELECT_LINKER) || ${SELECT_LINKER:Mld.gold}
SUBDIR+=	block3
.endif

SUBDIR_ORDERED= ${SUBDIR}

.include <bsd.subdir.mk>
