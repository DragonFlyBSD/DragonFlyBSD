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

# Ideally this should be -lncursesw
# Wide version can be used when DragonFly 3.3 branch is created
DPADD=		${LIBNCURSES}
LDADD=		-lncurses

ncurses_def.h:
	AWK=awk sh ${NCURSESDIR}/include/MKncurses_def.sh \
	  ${NCURSESDIR}/include/ncurses_defs > ${.TARGET}

parametrized.h:
	AWK=awk sh ${NCURSESDIR}/include/MKparametrized.sh \
	  ${TERMINFO_CAPS} > ${.TARGET}

# Hack: It's really termsort.c, but bootstrap stage chokes on it
termsort.h:
	sh ${PROGSDIR}/MKtermsort.sh awk ${TERMINFO_CAPS} > termsort.c

tic.o: ${PROGSDIR}/tic.c
	${CC} ${CFLAGS} -c ${PROGSDIR}/tic.c -o ${.TARGET}

transform.o: ${PROGSDIR}/transform.c
	${CC} ${CFLAGS} -c ${PROGSDIR}/transform.c -o ${.TARGET}

dump_entry.o: ${PROGSDIR}/dump_entry.c termsort.h
	${CC} ${CFLAGS} -c ${PROGSDIR}/dump_entry.c -o ${.TARGET}

CLEANFILES=	ncurses_def.h parametrized.h termsort.c

SRCS=	ncurses_def.h parametrized.h termsort.h
SRCS+=	tic.c dump_entry.c transform.c

.include <bsd.prog.mk>
