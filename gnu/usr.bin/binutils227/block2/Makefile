# Build in parallel
SUBDIR=	addr2line \
	ar \
	cxxfilt \
	elfedit \
	gas \
	gprof \
	nm \
	objcopy \
	objdump \
	ranlib \
	readelf \
	size \
	strings \
	strip

# Don't build gold linker during stage 3 cross-tools unless
# WORLD_LDVER has been set to ld.gold (and then don't build ld.bfd)
.if defined(SELECT_LINKER)
. if ${SELECT_LINKER:Mld.gold}
SUBDIR+=	libgold
. else
SUBDIR+=	ld
. endif
.else
SUBDIR+=	ld libgold
.endif

SUBDIR_ORDERED=

.include <bsd.subdir.mk>
