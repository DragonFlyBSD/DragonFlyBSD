.include "../Makefile.inc0"

.PATH: ${SRCDIR}/libiberty

LIB=	iberty
SRCS=	argv.c			\
	choose-temp.c		\
	concat.c		\
	cp-demangle.c		\
	cp-demint.c		\
	cplus-dem.c		\
	crc32.c			\
	dyn-string.c		\
	filename_cmp.c		\
	floatformat.c		\
	getopt.c		\
	getopt1.c		\
	getpwd.c		\
	getruntime.c		\
	hashtab.c		\
	hex.c			\
	lbasename.c		\
	lrealpath.c		\
	make-relative-prefix.c	\
	make-temp-file.c	\
	md5.c			\
	mempcpy.c		\
	objalloc.c		\
	obstack.c		\
	safe-ctype.c		\
	setproctitle.c		\
	sha1.c			\
	simple-object.c		\
	simple-object-coff.c	\
	simple-object-elf.c	\
	simple-object-mach-o.c	\
	stpcpy.c		\
	unlink-if-ordinary.c	\
	xatexit.c		\
	xexit.c			\
	xmalloc.c		\
	xstrdup.c		\
	xstrerror.c		\
	xstrndup.c
WARNS=	1
CFLAGS+=	-DHAVE_CONFIG_H
INTERNALLIB=	true

.include <bsd.lib.mk>
