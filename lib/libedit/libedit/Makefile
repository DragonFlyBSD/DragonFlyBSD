# NETBSD's libedit (BSD replacement for GNU readline)
.include	"../Makefile.inc"

LIB=		edit
SHLIB_MAJOR=	5  		# NetBSD designates SHLIB_MAJOR as 3

SRCDIR= 	${BASEDIR}/src
.PATH:		${SRCDIR}

CFLAGS+=	-I${SRCDIR} -I${.CURDIR} -I${.OBJDIR}

DPADD=		${LIBNCURSES}
LDADD=		-lncurses

WARNS?=		2

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
		readline.c

SRCS+=		common.h emacs.h fcns.h help.h vi.h
SRCS+=  	fcns.c help.c

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

.include <bsd.lib.mk>
