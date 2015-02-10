# All the libraries are standalone except the LTO plugin
# The plugin is needed by the compiler to functional normally, so it
# is created along with the backends and drivers rather than with the
# language libraries

SUBDIR_ORDERED= libiberty-pic liblto_plugin

SUBDIR=		libbacktrace
SUBDIR+=	libcommon
SUBDIR+=	libcommon-target
SUBDIR+=	libcpp
SUBDIR+=	libdecnumber
SUBDIR+=	libiberty
SUBDIR+=	libiberty-pic
SUBDIR+=	liblto_plugin

.include <bsd.subdir.mk>
