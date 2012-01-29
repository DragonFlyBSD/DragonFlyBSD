.include "../Makefile.inc0"
.PATH: ${SRCDIR}/info ${SRCDIR}/doc

PROG=		info
MAN=		info.1 info.5 texinfo.5

LDADD+=		-lncurses
DPADD+=		${LIBNCURSES}
CFLAGS+=	-I${SRCDIR} -I${SRCDIR}/lib -I${SRCDIR}/gnulib/lib
CFLAGS+=	-I${SRCDIR}/info -I${.CURDIR}/../libgnu
CFLAGS+=	-DHAVE_CONFIG_H

# Additional info dirs
#
CFLAGS+=-DINFODIR=\"/usr/pkg/info\"

SRCS=	dir.c \
	display.c \
	dribble.c \
	echo-area.c \
	filesys.c \
	footnotes.c \
	gc.c \
	indices.c \
	info-utils.c \
	info.c \
	infodoc.c \
	infomap.c \
	m-x.c \
	man.c \
	nodemenu.c \
	nodes.c \
	search.c \
	session.c \
	signals.c \
	terminal.c \
	tilde.c \
	variables.c \
	window.c \
	doc.c

.include <bsd.prog.mk>
