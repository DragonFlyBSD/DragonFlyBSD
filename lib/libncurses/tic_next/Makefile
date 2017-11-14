NCURSESDIR=	${.CURDIR}/../../../contrib/ncurses
TERMINFO_CAPS=	${NCURSESDIR}/include/Caps
.PATH:		${NCURSESDIR}/progs

PROG=		tic_next

CFLAGS+=	-D_XOPEN_SOURCE_EXTENDED
CFLAGS+=	-DENABLE_WIDEC
CFLAGS+=	-DSET_NCURSES_CH_T=cchar_t
CFLAGS+=	-DSET_NEED_WCHAR_H=1
CFLAGS+=	-I${NCURSESDIR}/progs
CFLAGS+=	-I.
CFLAGS+=	-I${NCURSESDIR}/include
CFLAGS+=	-I${.OBJDIR}/../libtic
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.OBJDIR}

LDADD+=		../libtic/libtic.na

ncurses_def.h:	MKncurses_def.sh ncurses_defs
	AWK=awk sh ${NCURSESDIR}/include/MKncurses_def.sh \
	  ${NCURSESDIR}/include/ncurses_defs > ${.TARGET}

parametrized.h: termsort.c
	AWK=awk sh ${NCURSESDIR}/include/MKparametrized.sh \
	  ${TERMINFO_CAPS} > ${.TARGET}

termsort.c:
	sh ${NCURSESDIR}/progs/MKtermsort.sh awk ${TERMINFO_CAPS} > termsort.c

tic_next.nx: ../libtic/libtic.na

CLEANFILES=	ncurses_def.h parametrized.h termsort.c

SRCS=	ncurses_def.h parametrized.h tic.c dump_entry.c tparm_type.c transform.c

# this tic_next is used in place, it is not installed anywhere
install:

.include <bsd.hostprog.mk>
