# libpam is normally built earlier by the _prebuild_libs target.
# Modules link in the shared libpam.so, but static_libpam links in the static
# modules so all modules need to be built before the static_libpam.

SUBDIR+=	libpam modules static_libpam
SUBDIR_ORDERED=	modules static_libpam

.include <bsd.subdir.mk>
