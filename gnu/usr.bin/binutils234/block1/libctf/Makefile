.include "../Makefile.block1"

.PATH: ${SRCDIR}/libctf

LIB=	ctf
SRCS=	ctf-archive.c ctf-dump.c ctf-create.c ctf-decl.c ctf-error.c \
	ctf-hash.c ctf-labels.c ctf-link.c ctf-lookup.c ctf-open.c \
	ctf-string.c ctf-subr.c ctf-types.c ctf-util.c ctf-open-bfd.c

CFLAGS+=	-DHAVE_CONFIG_H -I${SRCDIR}/include -I${SRCDIR}/libctf
CFLAGS+=	-I${SRCDIR}/bfd -I${.CURDIR} -I${.OBJDIR}
INTERNALLIB=	true

${SRCS}: bfd.h

CLEANFILES=	bfd.h

.include <bsd.lib.mk>
