# $FreeBSD: src/gnu/usr.bin/binutils/libbinutils/Makefile,v 1.8.2.5 2002/09/01 23:39:16 obrien Exp $
# $DragonFly: src/gnu/usr.bin/binutils/libbinutils/Attic/Makefile,v 1.2 2003/06/17 04:25:44 dillon Exp $

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/binutils

LIB=	binutils
SRCS+=	arlex.l arparse.y arsup.c bucomm.c debug.c filemode.c \
	ieee.c rdcoff.c rddbg.c rename.c stabs.c unwind-ia64.c \
	wrstabs.c version.c
WARNS=	0
CFLAGS+= -DTARGET=\"${TARGET_ARCH}-unknown-freebsd\"
CFLAGS+= -DBFD_VERSION_STRING=\"${VERSION}\"
CFLAGS+= -I${SRCDIR}/binutils
CFLAGS+= -I${SRCDIR}/bfd
INTERNALLIB=	true

.include <bsd.lib.mk>
