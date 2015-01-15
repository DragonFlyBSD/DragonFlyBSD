# NETBSD's libedit (BSD replacement for GNU readline)
.include	"../Makefile.inc"

LIB=		edit
SHLIB_MAJOR=	5  		# NetBSD designates SHLIB_MAJOR as 3
SHLIBDIR?=	/lib

SRCDIR= 	${BASEDIR}/src
.PATH:		${SRCDIR}

CFLAGS+=	-I${SRCDIR} -I${.CURDIR} -I${.OBJDIR}

DPADD=		${LIBNCURSES}
LDADD=		-lncurses

WARNS?=		2

.if defined(RELEASE36)
SHLIB_MAJOR=	6
VERSION_DEF=	${.CURDIR}/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map
.endif

SRCS+=		chared.c \
		common.c \
		el.c \
		hist.c \
		emacs.c \
		keymacro.c \
		map.c \
		chartype.c \
		parse.c \
		prompt.c \
		read.c \
		refresh.c \
		search.c \
		sig.c \
		terminal.c \
		tty.c \
		vi.c \
		fgetln.c \
		strlcat.c \
		strlcpy.c \
		unvis.c \
		vis.c \
		wcsdup.c \
		tokenizer.c \
		history.c \
		filecomplete.c \
		readline.c \
		eln.c

SRCS+=		common.h emacs.h fcns.h help.h vi.h
SRCS+=  	fcns.c help.c tokenizern.c historyn.c

INCS=		histedit.h

MAN=		editline.3 editrc.5

MLINKS= 	editline.3 el_init.3 \
		editline.3 el_end.3 \
		editline.3 el_reset.3 \
		editline.3 el_gets.3 \
		editline.3 el_getc.3 \
		editline.3 el_push.3 \
		editline.3 el_parse.3 \
		editline.3 el_set.3 \
		editline.3 el_get.3 \
		editline.3 el_source.3 \
		editline.3 el_resize.3 \
		editline.3 el_line.3 \
		editline.3 el_insertstr.3 \
		editline.3 el_deletestr.3 \
		editline.3 history_init.3 \
		editline.3 history_end.3 \
		editline.3 history.3 \
		editline.3 tok_init.3 \
		editline.3 tok_end.3 \
		editline.3 tok_reset.3 \
		editline.3 tok_line.3 \
		editline.3 tok_str.3

MLINKS+=	editline.3 el_wgets.3 \
		editline.3 el_wgetc.3 \
		editline.3 el_wpush.3 \
		editline.3 el_wparse.3 \
		editline.3 el_wset.3 \
		editline.3 el_wget.3 \
		editline.3 el_wline.3 \
		editline.3 el_winsertstr.3 \
		editline.3 el_wdeletestr.3 \
		editline.3 history_winit.3 \
		editline.3 history_wend.3 \
		editline.3 history_w.3 \
		editline.3 tok_winit.3 \
		editline.3 tok_wend.3 \
		editline.3 tok_wreset.3 \
		editline.3 tok_wline.3 \
		editline.3 tok_wstr.3

CLEANFILES+=	common.h emacs.h fcns.c fcns.h help.c help.h vi.h editline.c
AHDR=		vi.h emacs.h common.h
ASRC=		${SRCDIR}/vi.c ${SRCDIR}/emacs.c ${SRCDIR}/common.c

vi.h:
	AWK=awk sh ${SRCDIR}/makelist -h ${SRCDIR}/vi.c > ${.TARGET}

emacs.h:
	AWK=awk sh ${SRCDIR}/makelist -h ${SRCDIR}/emacs.c > ${.TARGET}

common.h:
	AWK=awk sh ${SRCDIR}/makelist -h ${SRCDIR}/common.c > ${.TARGET}

fcns.h: ${AHDR}
	AWK=awk sh ${SRCDIR}/makelist -fh ${AHDR} > ${.TARGET}

help.h:
	AWK=awk sh ${SRCDIR}/makelist -bh ${ASRC} > ${.TARGET}

fcns.c: ${AHDR}
	AWK=awk sh ${SRCDIR}/makelist -fc ${AHDR} > ${.TARGET}

help.c: ${ASRC}
	AWK=awk sh ${SRCDIR}/makelist -bc ${ASRC} > ${.TARGET}

tokenizern.c: tokenizer.c
	AWK=awk sh ${SRCDIR}/makelist -n ${.ALLSRC} > ${.TARGET}

historyn.c: history.c
	AWK=awk sh ${SRCDIR}/makelist -n ${.ALLSRC} > ${.TARGET}

.include <bsd.lib.mk>
