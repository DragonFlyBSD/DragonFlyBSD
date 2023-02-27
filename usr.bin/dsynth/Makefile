#
#

PROG=	dsynth
SRCS=	dsynth.c subs.c pkglist.c config.c bulk.c build.c repo.c mount.c
SRCS+=	status.c numa.c
SRCS+=	runstats.c ncurses.c monitor.c html.c
SRCS+=	icrc32.c

SCRIPTS=mktemplate.sh
SCRIPTSDIR= ${SHAREDIR}/dsynth

# NOTE: The HTML support was directly transplanted from synth, written by
#       John R. Marino <draco@marino.st>.
#
FILESDIR= ${SCRIPTSDIR}
FILES= favicon.png progress.html progress.css progress.js dsynth.png

CFLAGS+=	-pthread
CFLAGS+=	-DSCRIPTDIR=${SHAREDIR}/dsynth
LDADD+=		-lpthread -lutil
DPADD+=		${LIBPTHREAD} ${LIBUTIL}

# ncurses, md5, for DragonFlyBSD
#
CFLAGS+=        -I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
CFLAGS+=	${PRIVATELIB_CFLAGS}
LDFLAGS+=       ${PRIVATELIB_LDFLAGS}
LDADD+=		-lprivate_ncurses
LDADD+=		-lprivate_crypto
DPADD+=		${LIBNCURSES}
DPADD+=		${LIBCRYPTO}

LDADD+=		-lm
DPADD+=		${LIBM}

beforeinstall:
	mkdir -p ${DESTDIR}${SCRIPTSDIR}

.include <bsd.prog.mk>
