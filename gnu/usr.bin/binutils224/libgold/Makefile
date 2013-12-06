.include "../Makefile.inc0"

.PATH: ${SRCDIR}/gold

LIB=	gold
SRCS+=	archive.cc		\
	attributes.cc		\
	binary.cc		\
	common.cc		\
	compressed_output.cc	\
	copy-relocs.cc		\
	cref.cc			\
	defstd.cc		\
	descriptors.cc		\
	dirsearch.cc		\
	dwarf_reader.cc		\
	dynobj.cc		\
	ehframe.cc		\
	errors.cc		\
	expression.cc		\
	fileread.cc		\
	gc.cc			\
	gdb-index.cc		\
	gold-threads.cc		\
	gold.cc			\
	icf.cc			\
	incremental.cc		\
	int_encoding.cc		\
	layout.cc		\
	mapfile.cc		\
	merge.cc		\
	nacl.cc			\
	object.cc		\
	options.cc		\
	output.cc		\
	parameters.cc		\
	plugin.cc		\
	readsyms.cc		\
	reduced_debug_output.cc	\
	reloc.cc		\
	resolve.cc		\
	script-sections.cc	\
	script.cc		\
	stringpool.cc		\
	symtab.cc		\
	target.cc		\
	target-select.cc	\
	timer.cc		\
	version.cc		\
	workqueue-threads.cc	\
	workqueue.cc		\
	yyscript.c		\
	mremap.c

WARNS=	1
CFLAGS=  -DHAVE_CONFIG_H
CFLAGS+= -DLARGEFILE_SOURCE
CFLAGS+= -D_FILE_OFFSET_BITS=64
CFLAGS+= -DGOLD_DEFAULT_MACHINE=${GOLD_DEFAULT_MACHINE}
CFLAGS+= -DGOLD_DEFAULT_SIZE=${GOLD_DEFAULT_SIZE}
CFLAGS+= -DBINDIR="\"${TOOLS_PREFIX}${BINDIR}\""
CFLAGS+= -DTOOLBINDIR="\"${TOOLS_PREFIX}${BINDIR}\""
CFLAGS+= -DTOOLLIBDIR="\"${TOOLS_PREFIX}${LIBDIR}\""
CFLAGS+= -DUSRDATA_PREFIX="\"${USRDATA_PREFIX}\""
CFLAGS+= -I. -I${.CURDIR}
CFLAGS+= -I${SRCDIR}/gold
CFLAGS+= -I${SRCDIR}/include -I${SRCDIR}/elfcpp

INTERNALLIB=	true

DPADD=
LDADD=

.include <bsd.lib.mk>
