# $DragonFly: src/gnu/usr.bin/cc34/c++/Makefile,v 1.1 2004/06/14 22:27:52 joerg Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

.PATH:	${GCCDIR}/gcc/cp

PROG=	c++
NOMAN=	1
LINKS=	${BINDIR}/c++ ${BINDIR}/g++
LINKS+=	${BINDIR}/c++ ${BINDIR}/CC

SRCS=	gcc_local.c g++spec.c intl.c prefix.c version_local.c
SRCS+=	insn-constants.h insn-flags.h specs.h
SRCS+=	concat.c make-relative-prefix.c lbasename.c lrealpath.c
SRCS+=	make-temp-file.c obstack.c pex-unix.c safe-ctype.c xexit.c
SRCS+=	xmalloc.c xstrdup.c xmemdup.c xstrerror.c

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target}\"

.include <bsd.prog.mk>
