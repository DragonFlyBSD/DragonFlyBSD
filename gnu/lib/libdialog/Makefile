CONTRIBDIR=	${.CURDIR}/../../../contrib/dialog
.PATH:	${CONTRIBDIR}

LIB=	dialog
SHLIB_MAJOR= 6
MAN=	dialog.3
INCS=	dialog.h dlg_colors.h dlg_config.h dlg_internals.h dlg_keys.h
SRCS=	argv.c arrows.c buildlist.c buttons.c \
	calendar.c checklist.c columns.c dlg_keys.c editbox.c \
	formbox.c fselect.c guage.c help.c inputbox.c inputstr.c \
	menubox.c mixedform.c mixedgauge.c mouse.c mousewget.c msgbox.c \
	pause.c prgbox.c progressbox.c rangebox.c rc.c \
	tailbox.c textbox.c timebox.c trace.c treeview.c \
	ttysize.c ui_getc.c util.c version.c yesno.c
WARNS?=	3

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
CFLAGS+=	-DNCURSES_WIDECHAR

.include <bsd.lib.mk>
