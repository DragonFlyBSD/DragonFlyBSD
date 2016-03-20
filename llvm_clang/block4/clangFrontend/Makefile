# Static makefile
.include "${.CURDIR}/Makefile.sources"
CFLAGS+= ${CFLAGS_CLANG}
CFLAGS+= -DDF_CLANG_HEADERS=\"${USRDATA_PREFIX}/usr/libdata/clang\"
.include <bsd.lib.mk>
