# $DragonFly: src/gnu/usr.bin/gdb/libbfd/Makefile,v 1.1 2004/10/23 17:02:38 joerg Exp $

.include "../Makefile.inc"

LIB=	bfd
INTERNALLIB=	true

CONTRIBDIR=	${BASEDIR}/bfd
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${BASEDIR}/include -I${CONTRIBDIR} -I${.CURDIR} -I${.OBJDIR}

SRCS=	archive.c archive64.c archures.c bfd.c bfdio.c bfdwin.c binary.c
SRCS+=	cache.c coffgen.c cofflink.c corefile.c dwarf1.c dwarf2.c
SRCS+=	elf-eh-frame.c elf-strtab.c elf.c elflink.c
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

.include "Makefile.${MACHINE_ARCH}"

.include <bsd.lib.mk>
