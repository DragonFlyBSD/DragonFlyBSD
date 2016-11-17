# NETBSD's libedit (BSD replacement for GNU readline)
.include	"../Makefile.inc"

LIB=		private_edit
SHLIB_MAJOR=	5
SHLIBDIR?=	/lib
PRIVATELIB=

SRCDIR= 	${BASEDIR}/src
.PATH:		${SRCDIR}

CFLAGS+=	-I${SRCDIR} -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses

DPADD=		${LIBNCURSES}
LDADD=		-lprivate_ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

WARNS?=		2

VERSION_DEF=	${.CURDIR}/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map

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
		vis.c \
		wcsdup.c \
		tokenizer.c \
		history.c \
		filecomplete.c \
		readline.c \
		eln.c

# only when 1) !HAVE_STRLCAT  2) !HAVE_STRLCPY  3) !HAVE_VIS
#SRCS+=		strlcat.c \
		strlcpy.c \
		unvis.c

SRCS+=		common.h emacs.h fcns.h help.h vi.h
SRCS+=  	fcns.c help.c tokenizern.c historyn.c

INCS=		histedit.h

MAN=		editrc.5

CLEANFILES+=	common.h emacs.h fcns.c fcns.h help.c help.h
CLEANFILES+=	historyn.c tokenizern.c vi.h
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
