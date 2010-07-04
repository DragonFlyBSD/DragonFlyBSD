INCLUDES = \
	-I$(top_srcdir)				\
	-I$(top_srcdir)/lib			\
	-DDATADIR=\""$(datadir)"\"		\
	-DLOCALEDIR=\""$(datadir)/locale"\"	\
	-DLIBDIR=\""$(libdir)"\"		\
	-DPREFIX=\""$(prefix)"\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\"	\
	-DVERSION=\""$(VERSION)"\"		\
	-D_GNU_SOURCE

cryptsetup_SOURCES = \
	cryptsetup.c				\
	cryptsetup.h

if STATIC_CRYPTSETUP
_STATIC_FLAGS = -all-static
_STATIC_LDADD = @LIBGCRYPT_LIBS@ -lgpg-error @SELINUX_STATIC_LIBS@
endif

cryptsetup_LDADD = \
	$(top_builddir)/lib/libcryptsetup.la	\
	$(_STATIC_LDADD)			\
	@POPT_LIBS@


cryptsetup_LDFLAGS = \
	$(_STATIC_FLAGS)

cryptsetup_CFLAGS = -Wall

sbin_PROGRAMS=cryptsetup
