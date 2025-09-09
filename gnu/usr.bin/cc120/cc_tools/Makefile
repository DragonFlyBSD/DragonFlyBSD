# Both libraries must be built before "tools"
# Just don't define SUBDIR_ORDERED and it will build serially, correctly

.if defined(LIBGCC_ONLY)
SUBDIR=  libiberty tools
.else
SUBDIR=  libcpp libcody libiberty tools
.endif

.include <bsd.obj.mk>
