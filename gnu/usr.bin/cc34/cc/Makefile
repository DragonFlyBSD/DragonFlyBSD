# $DragonFly: src/gnu/usr.bin/cc34/cc/Makefile,v 1.5 2007/01/13 16:32:16 corecode Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

PROG=	cc
LINKS=	${BINDIR}/cc ${BINDIR}/gcc
MLINKS=	cc.1 c++.1 cc.1 g++.1 cc.1 gcc.1

SRCS=	gcc.c gccspec.c intl.c prefix.c version_local.c
SRCS+=	insn-constants.h insn-flags.h specs.h
SRCS+=	concat.c make-relative-prefix.c lbasename.c lrealpath.c
SRCS+=	make-temp-file.c obstack.c pex-unix.c safe-ctype.c xexit.c
SRCS+=	xmalloc.c xstrdup.c xmemdup.c xstrerror.c

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target}\"

cc.1: ${GCCDIR}/gcc/doc/gcc.1
	cp ${.ALLSRC} ${.TARGET}

CLEANFILES+=	cc.1

.include <bsd.prog.mk>
