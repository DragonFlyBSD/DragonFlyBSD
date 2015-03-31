.include "../../Makefile.inc1"
.include "../../block3/Makefile.gld"

LIB=		gold
INTERNALLIB=	true

# copied from gold/Makefile
SRCS=	archive.cc \
	attributes.cc \
	binary.cc \
	common.cc \
	compressed_output.cc \
	copy-relocs.cc \
	cref.cc \
	defstd.cc \
	descriptors.cc \
	dirsearch.cc \
	dynobj.cc \
	dwarf_reader.cc \
	ehframe.cc \
	errors.cc \
	expression.cc \
	fileread.cc \
        gc.cc \
        gdb-index.cc \
	gold.cc \
	gold-threads.cc \
	icf.cc \
	incremental.cc \
	int_encoding.cc \
	layout.cc \
	mapfile.cc \
	merge.cc \
	nacl.cc \
	object.cc \
	options.cc \
	output.cc \
	parameters.cc \
	plugin.cc \
	readsyms.cc \
	reduced_debug_output.cc \
	reloc.cc \
	resolve.cc \
	script-sections.cc \
	script.cc \
	stringpool.cc \
	symtab.cc \
	target.cc \
	target-select.cc \
	timer.cc \
	version.cc \
	workqueue.cc \
	workqueue-threads.cc 

# extra sources
SRCS+=	yyscript.c mremap.c

# required for both ld.gold and incremental-dump
# let's dump them in libgold.a so we don't have to build them twice

SRCS+=	i386.cc			\
	x86_64.cc		\
	sparc.cc		\
	powerpc.cc		\
	arm.cc			\
	arm-reloc-property.cc	\
	tilegx.cc		\
	mips.cc 		\
	aarch64.cc 		\
	aarch64-reloc-property.cc

.include <bsd.lib.mk>
