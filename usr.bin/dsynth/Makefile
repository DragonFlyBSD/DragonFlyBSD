#
#

PROG=	dsynth
SRCS=	dsynth.c subs.c pkglist.c config.c bulk.c build.c repo.c mount.c
SRCS+=	status.c
SRCS+=	runstats.c ncurses.c monitor.c html.c

SCRIPTS=mktemplate.sh
SCRIPTSDIR= ${SHAREDIR}/dsynth

CFLAGS+=	-pthread
CFLAGS+=	-DSCRIPTDIR=${SHAREDIR}/dsynth
LDADD+=		-lpthread -lutil
DPADD+=		${LIBPTHREAD} ${LIBUTIL}

# ncurses, DragonFlyBSD
#
CFLAGS+=        -I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=       ${PRIVATELIB_LDFLAGS}
LDADD+=		-lprivate_ncurses
DPADD+=		${LIBNCURSES}

LDADD+=		-lm
DPADD+=		${LIBM}

beforeinstall:
	mkdir -p ${DESTDIR}${SCRIPTSDIR}

.include <bsd.prog.mk>
