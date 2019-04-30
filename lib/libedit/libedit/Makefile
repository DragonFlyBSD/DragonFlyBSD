# NETBSD's libedit (BSD replacement for GNU readline)
.include	"../Makefile.inc"

LIB=		private_edit
SHLIB_MAJOR=	6
SHLIBDIR?=	/lib
PRIVATELIB=	shpub

SRCDIR= 	${BASEDIR}/src
.PATH:		${SRCDIR}

CFLAGS+=	-I${SRCDIR} -I${.CURDIR} -I${.OBJDIR}

CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
DPADD=		${LIBNCURSES}
LDADD=		-lprivate_ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

WARNS?=		2

# DO NOT ENABLE
.if 0
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
		tokenizer.c \
		history.c \
		filecomplete.c \
		readline.c \
		literal.c \
		eln.c

SRCS+=		tokenizern.c historyn.c

SRCS+=		common.h emacs.h fcns.h func.h help.h vi.h

INCS=		histedit.h
INCSDIR=	${INCLUDEDIR}/priv

MAN=		editrc.5

CLEANFILES+=	common.h emacs.h fcns.h func.h help.h vi.h
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

func.h: ${AHDR}
	AWK=awk sh ${SRCDIR}/makelist -fc ${AHDR} > ${.TARGET}

help.h:
	AWK=awk sh ${SRCDIR}/makelist -bh ${ASRC} > ${.TARGET}

.include <bsd.lib.mk>
