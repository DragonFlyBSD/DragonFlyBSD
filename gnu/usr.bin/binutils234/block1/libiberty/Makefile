.include "../Makefile.block1"

.PATH: ${SRCDIR}/libiberty

LIB=	iberty

# REQUIRED_OFILES
SRCS=	regex.c cplus-dem.c cp-demangle.c \
	md5.c sha1.c alloca.c \
	argv.c \
	choose-temp.c concat.c \
	cp-demint.c crc32.c d-demangle.c \
	dwarfnames.c dyn-string.c \
	fdmatch.c fibheap.c \
	filedescriptor.c \
	filename_cmp.c floatformat.c \
	fnmatch.c fopen_unlocked.c \
	getopt.c getopt1.c getpwd.c \
	getruntime.c hashtab.c hex.c \
	lbasename.c lrealpath.c \
	make-relative-prefix.c make-temp-file.c \
	objalloc.c \
	obstack.c \
	partition.c pexecute.c physmem.c \
	pex-common.c pex-one.c pex-unix.c \
	vprintf-support.c \
	rust-demangle.c \
	safe-ctype.c \
	simple-object.c simple-object-coff.c \
	simple-object-elf.c simple-object-mach-o.c \
	simple-object-xcoff.c \
	sort.c spaces.c \
	splay-tree.c stack-limit.c \
	strerror.c strsignal.c \
	timeval-utils.c unlink-if-ordinary.c \
	xasprintf.c xatexit.c xexit.c \
	xmalloc.c xmemdup.c xstrdup.c \
	xstrerror.c xstrndup.c \
	xvasprintf.c

#	strverscmp.c

CFLAGS+=	-DHAVE_CONFIG_H
INTERNALLIB=	true

.include <bsd.lib.mk>
