.include "../Makefile.inc"

LIB=		readline
INTERNALLIB=	YES

.PATH:		${BASEDIR}/readline
CFLAGS+=	-I${.CURDIR} -I${.OBJDIR} -I${BASEDIR}
CFLAGS+=	-DHAVE_CONFIG_H

SRCS+=		readline.c \
		vi_mode.c \
		funmap.c \
		keymaps.c \
		parens.c \
		search.c \
		rltty.c \
		complete.c \
		bind.c \
		isearch.c \
		display.c \
		signals.c \
		util.c \
		kill.c \
		undo.c \
		macro.c \
		input.c \
		callback.c \
		terminal.c \
		text.c \
		nls.c \
		misc.c \
		compat.c \
		xfree.c \
		xmalloc.c \
		history.c \
		histexpand.c \
		histfile.c \
		histsearch.c \
		shell.c \
		mbutil.c \
		tilde.c
		
.include <bsd.lib.mk>
