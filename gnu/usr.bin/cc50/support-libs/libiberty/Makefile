GCC_NO_PATH=	yes
GCC_NO_LIBS=	yes
LOCAL_CONFIG=	yes
.include "../Makefile.inc"
.PATH:	${GCCDIR}/libiberty

LIB=		iberty
INTERNALLIB=	YES

# Object files present after vendor-built libiberty created.
REQUIRED_OFILES= \
	alloca.o \
	argv.o \
	choose-temp.o \
	concat.o \
	cp-demangle.o \
	cp-demint.o \
	cplus-dem.o \
	crc32.o \
	d-demangle.o \
	dwarfnames.o \
	dyn-string.o \
	fdmatch.o \
	fibheap.o \
	filename_cmp.o \
	floatformat.o \
	fnmatch.o \
	fopen_unlocked.o \
	getopt.o \
	getopt1.o \
	getpwd.o \
	getruntime.o \
	hashtab.o \
	hex.o \
	lbasename.o \
	lrealpath.o \
	make-relative-prefix.o \
	make-temp-file.o \
	md5.o \
	objalloc.o \
	obstack.o \
	partition.o \
	pex-common.o \
	pex-one.o \
	pex-unix.o \
	pexecute.o \
	physmem.o \
	regex.o \
	safe-ctype.o \
	sha1.o \
	simple-object-coff.o \
	simple-object-elf.o \
	simple-object-mach-o.o \
	simple-object-xcoff.o \
	simple-object.o \
	sort.o \
	spaces.o \
	splay-tree.o \
	stack-limit.o\
	strerror.o \
	strsignal.o \
	strverscmp.o \
	timeval-utils.o \
	unlink-if-ordinary.o \
	vprintf-support.o \
	xatexit.o \
	xasprintf.o \
	xexit.o \
	xmalloc.o \
	xmemdup.o \
	xstrdup.o \
	xstrerror.o \
	xstrndup.o \
	xvasprintf.o

SRCS=	${REQUIRED_OFILES:T:.o=.c}

.include <bsd.lib.mk>
