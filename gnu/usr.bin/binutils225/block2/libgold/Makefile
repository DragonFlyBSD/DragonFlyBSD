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

.include <bsd.lib.mk>
