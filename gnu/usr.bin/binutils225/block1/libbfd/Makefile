.include "../Makefile.block1"

LIB=		bfd
INTERNALLIB=	true

CONTRIBDIR=	${SRCDIR}/bfd
.PATH:		${CONTRIBDIR}

CFLAGS+=	-I${SRCDIR}/include -I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-DDEBUGDIR='"/usr/lib/debug"'  # no clue why this is needed
FLAGS_GROUPS=	target

# BFD32_LIBS_CFILES 
SRCS=	archive.c archures.c bfd.c bfdio.c bfdwin.c \
	cache.c coffgen.c corefile.c \
	format.c init.c libbfd.c opncls.c reloc.c \
	section.c syms.c targets.c hash.c linker.c \
	srec.c binary.c tekhex.c ihex.c stabs.c stab-syms.c \
	merge.c dwarf2.c simple.c compress.c verilog.c

# from Makefile, BFD64_BACKENDS_CFILES  (x86-64 specific)
SRCS+=	elf64-x86-64.c elf-ifunc.c elf-nacl.c elf64.c elf.c elflink.c \
	elf-attrs.c elf-strtab.c elf-eh-frame.c dwarf1.c elf32-i386.c \
	elf-vxworks.c elf32.c elf64-gen.c elf32-gen.c

# ALL_MACHINES_CFILES
SRCS+=	cpu-i386.c cpu-l1om.c cpu-k1om.c

# BFD64_LIBS_CFILES
SRCS+=		archive64.c

VECTORS=	x86_64_elf64_vec \
		i386_elf32_vec \
		l1om_elf64_vec \
		k1om_elf64_vec \
		elf64_le_vec \
		elf64_be_vec \
		elf32_le_vec \
		elf32_be_vec 

DEFAULT_VECTOR=	x86_64_elf64_vec
ARCHITECTURES=	bfd_i386_arch bfd_l1om_arch bfd_k1om_arch

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
cache.c elf32-i386.c elf64-x86-64.c: bfd_stdint.h

bfd_stdint.h:
	touch ${.TARGET}

targmatch.h: config.bfd targmatch.sed
	sed -f ${CONTRIBDIR}/targmatch.sed < ${CONTRIBDIR}/config.bfd \
	> ${.TARGET}

elf32-target.h: elfxx-target.h
	sed -e s/NN/32/g < ${CONTRIBDIR}/elfxx-target.h > ${.TARGET}
elf64-target.h: elfxx-target.h
	sed -e s/NN/64/g < ${CONTRIBDIR}/elfxx-target.h > ${.TARGET}

CLEANFILES=	bfd_stdint.h bfd.h elf64-target.h elf32-target.h targmatch.h

.include <bsd.lib.mk>
