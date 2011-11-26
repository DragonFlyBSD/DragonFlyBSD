.include "../Makefile.inc0"
.PATH: ${SRCDIR}/makeinfo ${SRCDIR}/doc

PROG=		makeinfo

CFLAGS+=	-I${SRCDIR} -I${SRCDIR}/makeinfo -I${SRCDIR}/gnulib/lib
CFLAGS+=	-I${.CURDIR}/../libgnu
CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		cmds.c \
		defun.c \
		files.c \
		float.c \
		footnote.c \
		html.c \
		index.c \
		insertion.c \
		lang.c \
		macro.c \
		makeinfo.c \
		multi.c \
		node.c \
		sectioning.c \
		toc.c \
		xml.c \
		xref.c

.include <bsd.prog.mk>
