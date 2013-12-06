.include "../Makefile.inc0"

LIB=	bfd
INTERNALLIB=	true

CONTRIBDIR=	${SRCDIR}/bfd
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${SRCDIR}/include -I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
# XXX no clue what DEBUGDIR is for
CFLAGS+=	-DDEBUGDIR='"/usr/lib/debug"'

SRCS=	archive.c archures.c bfd.c bfdio.c bfdwin.c \
	cache.c coffgen.c corefile.c \
	format.c init.c libbfd.c opncls.c reloc.c \
	section.c syms.c targets.c hash.c linker.c \
	srec.c binary.c tekhex.c ihex.c stabs.c stab-syms.c \
	merge.c dwarf2.c simple.c compress.c verilog.c

SRCS+=		targmatch.h elf32-target.h elf64-target.h
CLEANFILES+=	elf64-target.h elf32-target.h targmatch.h

targmatch.h: config.bfd targmatch.sed
	sed -f ${CONTRIBDIR}/targmatch.sed < ${CONTRIBDIR}/config.bfd > ${.TARGET}

elf32-target.h: elfxx-target.h
	sed -e s/NN/32/g < ${CONTRIBDIR}/elfxx-target.h > ${.TARGET}
elf64-target.h: elfxx-target.h
	sed -e s/NN/64/g < ${CONTRIBDIR}/elfxx-target.h > ${.TARGET}

TARGET_ARCH?=	${MACHINE_ARCH}
.include "Makefile.${TARGET_ARCH}"

_selvec=
.for _v in ${VECTORS}
CFLAGS+= -DHAVE_${_v}
.if !empty(_selvec)
_selvec+= ,
.endif
_selvec+= &${_v}
.endfor
CFLAGS+= -DSELECT_VECS='${_selvec}'
CFLAGS+= -DSELECT_ARCHITECTURES='${ARCHITECTURES:S/^/\&/:S/$/,/} 0'
CFLAGS+= -DDEFAULT_VECTOR=${DEFAULT_VECTOR}


SRCS+=		bfd.h
CLEANFILES+=	bfd.h

# Used to do keyword replacement in bfd-X.h
#
BFD_HOST_64_BIT_DEFINED?=1
BFD_HOST_LONG_LONG?=1
.include "Makefile.host.${MACHINE_ARCH}"

bfd.h: 	${CONTRIBDIR}/bfd-in2.h
	sed -e "s/@supports_plugins@/0/g" \
	    -e "s/@wordsize@/${BFD_ARCH_SIZE}/g" \
	    -e "s/@bfd_default_target_size@/${BFD_DEFAULT_TARGET_SIZE}/g" \
	    -e "s/@BFD_HOST_64BIT_LONG@/${BFD_HOST_64BIT_LONG}/g" \
	    -e "s/@BFD_HOST_64BIT_LONG_LONG@/${BFD_HOST_64BIT_LONG_LONG}/g" \
	    -e "s/@BFD_HOST_64_BIT@/${BFD_HOST_64_BIT}/g" \
	    -e "s/@BFD_HOST_U_64_BIT@/${BFD_HOST_U_64_BIT}/g" \
	    -e "s/@BFD_HOST_64_BIT_DEFINED@/${BFD_HOST_64_BIT_DEFINED}/g" \
	    -e "s/@bfd_file_ptr@/${BFD_HOST_64_BIT}/g" \
	    -e "s/@BFD_HOSTPTR_T@/${BFD_HOSTPTR_T}/g" \
	    < ${CONTRIBDIR}/bfd-in2.h > bfd.h.new
	mv -f bfd.h.new bfd.h

SRCS+=		bfd_stdint.h
CLEANFILES+=	bfd_stdint.h

bfd_stdint.h:
	touch ${.TARGET}

.include <bsd.lib.mk>
