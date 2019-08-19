#
#

PROG=	dsynth
SRCS=	dsynth.c subs.c pkglist.c config.c bulk.c build.c repo.c mount.c gui.c
NOMAN=true

SCRIPTS=mktemplate.sh
SCRIPTSDIR= ${SHAREDIR}/dsynth

CFLAGS+=	-pthread
CFLAGS+=	-DSCRIPTDIR=${SHAREDIR}/dsynth
LDADD+=		-lpthread -lutil

# ncurses, DragonFlyBSD
#
CFLAGS+=        -I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=       ${PRIVATELIB_LDFLAGS}
LDADD+=		-lprivate_ncurses

LDADD+=		-lm

beforeinstall:
	mkdir -p ${SCRIPTSDIR}

.include <bsd.prog.mk>
