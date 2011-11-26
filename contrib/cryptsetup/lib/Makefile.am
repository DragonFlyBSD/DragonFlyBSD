moduledir = $(libdir)/cryptsetup

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libcryptsetup.pc

INCLUDES = \
	-I$(top_srcdir)				\
	-I$(top_srcdir)/luks			\
	-DDATADIR=\""$(datadir)"\"		\
	-DLIBDIR=\""$(libdir)"\"		\
	-DPREFIX=\""$(prefix)"\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\"	\
	-DVERSION=\""$(VERSION)"\"		\
	-D_GNU_SOURCE				\
	-D_LARGEFILE64_SOURCE			\
	-D_FILE_OFFSET_BITS=64

lib_LTLIBRARIES = libcryptsetup.la

if STATIC_LIBRARY
_STATIC_LIBRARY = -static
endif

libcryptsetup_la_DEPENDENCIES = libcryptsetup.sym

libcryptsetup_la_LDFLAGS = \
	$(_STATIC_LIBRARY) \
	-Wl,--version-script=$(top_srcdir)/lib/libcryptsetup.sym \
	-version-info @LIBCRYPTSETUP_VERSION_INFO@

libcryptsetup_la_CFLAGS = -Wall @LIBGCRYPT_CFLAGS@

libcryptsetup_la_LIBADD = \
	@UUID_LIBS@				\
	@DEVMAPPER_LIBS@			\
	@LIBGCRYPT_LIBS@			\
	../luks/libluks.la

libcryptsetup_la_SOURCES = \
	setup.c					\
	internal.h				\
	nls.h					\
	blockdev.h				\
	libcryptsetup.h				\
	utils.c					\
	utils_debug.c				\
	backends.c				\
	libdevmapper.c				\
	gcrypt.c

include_HEADERS = libcryptsetup.h

EXTRA_DIST = libcryptsetup.pc.in libcryptsetup.sym
