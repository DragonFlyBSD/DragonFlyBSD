# $DragonFly: src/gnu/usr.bin/cc34/g77/Attic/Makefile,v 1.1 2004/06/14 22:27:56 joerg Exp $

.include "../Makefile.inc"
.include "../cc_tools/Makefile.tools"
.include "../Makefile.langs"

.PATH:	${GCCDIR}/gcc/f

PROG=	g77
LINKS=	${BINDIR}/g77 ${BINDIR}/f77

SRCS=	gcc_local.c g77spec.c intl.c prefix.c version_local.c
SRCS+=	insn-constants.h insn-flags.h specs.h
SRCS+=	concat.c make-relative-prefix.c lbasename.c lrealpath.c
SRCS+=	make-temp-file.c obstack.c pex-unix.c safe-ctype.c xexit.c
SRCS+=	xmalloc.c xstrdup.c xmemdup.c xstrerror.c

CFLAGS+= -I.
CFLAGS+= -DDEFAULT_TARGET_VERSION=\"${version}\"
CFLAGS+= -DDEFAULT_TARGET_MACHINE=\"${target}\"

.include <bsd.prog.mk>
