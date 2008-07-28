# $DragonFly: src/gnu/usr.bin/gdb/libbfd/Makefile,v 1.3 2008/07/28 22:33:21 corecode Exp $

.include "../Makefile.inc"

LIB=	bfd
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/bfd
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${BASEDIR}/include -I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}
# XXX no clue what DEBUGDIR is for
CFLAGS+=	-DDEBUGDIR='"/usr/lib/debug"'

SRCS=	archive.c archive64.c archures.c bfd.c bfdio.c bfdwin.c binary.c
SRCS+=	cache.c coffgen.c cofflink.c corefile.c dwarf1.c dwarf2.c
SRCS+=	elf-eh-frame.c elf-strtab.c elf.c elflink.c elf-attrs.c
SRCS+=	format.c hash.c ihex.c init.c libbfd.c linker.c merge.c opncls.c
SRCS+=	reloc.c section.c simple.c srec.c stab-syms.c stabs.c syms.c
SRCS+=	targets.c tekhex.c

SRCS+=		targmatch.h
CLEANFILES+=	elf64-target.h elf32-target.h targmatch.h

targmatch.h: config.bfd targmatch.sed
	sed -f ${CONTRIBDIR}/targmatch.sed < ${CONTRIBDIR}/config.bfd > ${.TARGET}

elf32-target.h: elfxx-target.h
	sed -e s/NN/32/g < ${CONTRIBDIR}/elfxx-target.h > ${.TARGET}
elf64-target.h: elfxx-target.h
	sed -e s/NN/64/g < ${CONTRIBDIR}/elfxx-target.h > ${.TARGET}

TARGET_ARCH?=	${MACHINE_ARCH}
.include "Makefile.${TARGET_ARCH}"

SRCS+=		bfd.h bfdver.h
CLEANFILES+=	bfd.h bfdver.h

# Used to do keyword replacement in bfd-X.h
#
BFD_ARCH_SIZE?=64
BFD_HOST_64_BIT_DEFINED?=1
BFD_HOST_LONG_LONG?=1
.include "Makefile.host.${MACHINE_ARCH}"

bfd.h: 	${CONTRIBDIR}/bfd-in2.h
	sed -e "s/@wordsize@/${BFD_ARCH_SIZE}/g" \
	    -e "s/@bfd_default_target_size@/${BFD_DEFAULT_TARGET_SIZE}/g" \
	    -e "s/@BFD_HOST_64BIT_LONG@/${BFD_HOST_64BIT_LONG}/g" \
	    -e "s/@BFD_HOST_64BIT_LONG_LONG@/${BFD_HOST_64BIT_LONG_LONG}/g" \
	    -e "s/@BFD_HOST_64_BIT@/${BFD_HOST_64_BIT}/g" \
	    -e "s/@BFD_HOST_U_64_BIT@/${BFD_HOST_U_64_BIT}/g" \
	    -e "s/@BFD_HOST_64_BIT_DEFINED@/${BFD_HOST_64_BIT_DEFINED}/g" \
	    -e "s/@BFD_HOST_LONG_LONG@/${BFD_HOST_LONG_LONG}/g" \
	    -e "s/@bfd_file_ptr@/${BFD_HOST_64_BIT}/g" \
	    -e "s/@BFD_HOSTPTR_T@/${BFD_HOSTPTR_T}/g" \
	    < ${CONTRIBDIR}/bfd-in2.h > bfd.h.new
	mv -f bfd.h.new bfd.h

bfdver.h: ${CONTRIBDIR}/version.h
	sed -e "s/@bfd_version_string@/\"2.18.50.20070907\"/g" \
	    -e "s/@bfd_version_package@/\"(GNU Binutils, GDB packaged)\"/g" \
	    -e "s/@bfd_version@/218500000/g" \
	    < ${CONTRIBDIR}/version.h > bfdver.h.new
	mv -f bfdver.h.new bfdver.h

SRCS+=		bfd_stdint.h
CLEANFILES+=	bfd_stdint.h

bfd_stdint.h:
	touch ${.TARGET}

.include <bsd.lib.mk>
