.include "../../Makefile.inc1"
.include "../Makefile.block1"

LIB=		bfd
INTERNALLIB=	true

CONTRIBDIR=	${SRCDIR}/bfd
.PATH:		${CONTRIBDIR}

CFLAGS+=	-I${SRCDIR}/include -I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-DDEBUGDIR='"/usr/lib/debug"'  # no clue why this is needed
FLAGS_GROUPS=	target plugins

# BFD32_LIBS_CFILES
SRCS=	archive.c archures.c bfd.c bfdio.c cache.c \
	coff-bfd.c compress.c corefile.c elf-properties.c format.c \
	hash.c init.c libbfd.c linker.c merge.c opncls.c reloc.c \
	section.c simple.c stab-syms.c stabs.c syms.c targets.c \
	binary.c ihex.c srec.c tekhex.c verilog.c

# only when USE_MMAP (--with-mmap)
#SRCS+= bfdwin.c

# from Makefile, BFD_BACKENDS  (x86-64 specific)
SRCS+=	elf64-x86-64.co elfxx-x86.c elf-ifunc.c elf-nacl.c elf-vxworks.c \
	elf64.c elf.c elflink.c elf-attrs.c elf-strtab.c elf-eh-frame.c \
	dwarf1.c dwarf2.c elf32-i386.c elf32.c \
	pei-x86_64.c pex64igen.c cofflink.c coffgen.c \
	elf64-gen.c elf32-gen.c

# ALL_MACHINES_CFILES
SRCS+=	cpu-i386.c cpu-iamcu.c cpu-l1om.c cpu-k1om.c

# BFD64_LIBS_CFILES
SRCS+=		archive64.c

VECTORS=	x86_64_elf64_vec \
		x86_64_pei_vec \
		i386_elf32_vec \
		iamcu_elf32_vec \
		l1om_elf64_vec \
		k1om_elf64_vec \
		elf64_le_vec \
		elf64_be_vec \
		elf32_le_vec \
		elf32_be_vec

# plugin support mainly for LTO ar, nm, ranlib
SRCS+=		plugin.c
VECTORS+=	plugin_vec
# allow users to put plugin dirs in common place for testing, but use relative to exec
plugins_FLAGS+=	-DBINDIR=\"${TOOLS_PREFIX}${BINDIR}\" -DPLUGINLIBDIR=\"/usr/lib\"
plugins_FLAGS+=	-DRELPLUGINDIR=\"/../bfd-plugins\" # standard one uses lib/bfd-plugins
plugins_FLAGS_FILES=	plugin.c

DEFAULT_VECTOR=	x86_64_elf64_vec
ARCHITECTURES=	bfd_i386_arch bfd_iamcu_arch bfd_l1om_arch bfd_k1om_arch

_selvec=
.for _v in ${VECTORS}
target_FLAGS+= -DHAVE_${_v}
.if !empty(_selvec)
_selvec+= ,
.endif
_selvec+= &${_v}
.endfor
target_FLAGS+= -DSELECT_VECS='${_selvec}'
target_FLAGS+= -DSELECT_ARCHITECTURES='${ARCHITECTURES:S/^/\&/:S/$/,/} 0'
target_FLAGS+= -DDEFAULT_VECTOR=${DEFAULT_VECTOR}
target_FLAGS_FILES=	targets.c archures.c

${SRCS}: bfd.h
targets.c: targmatch.h
elf32-gen.c elf32-i386.c elf64-x86-64.c: elf32-target.h
elf64-gen.c elf64-x86-64.c: elf64-target.h

targmatch.h: config.bfd targmatch.sed
	sed -f ${CONTRIBDIR}/targmatch.sed < ${CONTRIBDIR}/config.bfd \
	> ${.TARGET}

elf32-target.h: elfxx-target.h
	sed -e s/NN/32/g < ${CONTRIBDIR}/elfxx-target.h > ${.TARGET}
elf64-target.h: elfxx-target.h
	sed -e s/NN/64/g < ${CONTRIBDIR}/elfxx-target.h > ${.TARGET}
pex64igen.c: peXXigen.c
	sed -e s/XX/pex64/g < ${CONTRIBDIR}/peXXigen.c > ${.TARGET}

CLEANFILES=	bfd.h elf64-target.h elf32-target.h targmatch.h pex64igen.c

.include <bsd.lib.mk>
