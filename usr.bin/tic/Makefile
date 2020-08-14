NCURSESDIR=	${.CURDIR}/../../contrib/ncurses
TERMINFO_CAPS=  ${NCURSESDIR}/include/Caps
PROGSDIR=	${NCURSESDIR}/progs
.PATH:		${PROGSDIR}

PROG=		tic

CFLAGS+=	-D_XOPEN_SOURCE_EXTENDED
CFLAGS+=	-DENABLE_WIDEC
CFLAGS+=	-DSET_NCURSES_CH_T=cchar_t
CFLAGS+=	-DSET_NEED_WCHAR_H=1
CFLAGS+=	-I${PROGSDIR}
CFLAGS+=	-I.
CFLAGS+=	-I${NCURSESDIR}/include
CFLAGS+=	-I${.CURDIR}/../../lib/libncurses/include
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}

DPADD=		${LIBNCURSESW}
LDADD=		-lprivate_ncursesw
CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

term.h:
	awk -f ${.CURDIR}/../../lib/libncurses/include/MKterm.h.awk ${TERMINFO_CAPS} \
	  > ${.TARGET}.new
	sh ${NCURSESDIR}/include/edit_cfg.sh ${.CURDIR}/../../libncurses/include/ncurses_cfg.h \
	  ${.TARGET}.new
	mv -f ${.TARGET}.new ${.TARGET}

ncurses_def.h:
	AWK=awk sh ${NCURSESDIR}/include/MKncurses_def.sh \
	  ${NCURSESDIR}/include/ncurses_defs > ${.TARGET}

parametrized.h:
	AWK=awk sh ${NCURSESDIR}/include/MKparametrized.sh \
	  ${TERMINFO_CAPS} > ${.TARGET}

beforedepend:	termsort.c

termsort.c:
	sh ${PROGSDIR}/MKtermsort.sh awk ${TERMINFO_CAPS} > termsort.c

tic.o: ${PROGSDIR}/tic.c
	${CC} ${CFLAGS} -c ${PROGSDIR}/tic.c -o ${.TARGET}

transform.o: ${PROGSDIR}/transform.c
	${CC} ${CFLAGS} -c ${PROGSDIR}/transform.c -o ${.TARGET}

dump_entry.o: ${PROGSDIR}/dump_entry.c termsort.c
	${CC} ${CFLAGS} -c ${PROGSDIR}/dump_entry.c -o ${.TARGET}

CLEANFILES=	ncurses_def.h parametrized.h term.h termsort.c

SRCS=	ncurses_def.h parametrized.h term.h
SRCS+=	tic.c dump_entry.c tparm_type.c transform.c

.include <bsd.prog.mk>
