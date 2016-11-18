#
# $DragonFly: src/usr.sbin/installer/dfuife_curses/Makefile,v 1.3 2008/08/29 22:51:56 swildner Exp $

PROG=	dfuife_curses
NOMAN=
SRCS=	curses_bar.c curses_form.c curses_util.c curses_widget.c \
	curses_xlat.c main.c

CFLAGS+=-I${.CURDIR}/..

.if exists(${.OBJDIR}/../libaura)
LIBAURA=${.OBJDIR}/../libaura/libaura.a
.else
LIBAURA=${.CURDIR}/../libaura/libaura.a
.endif

.if exists(${.OBJDIR}/../libdfui)
LIBDFUI=${.OBJDIR}/../libdfui/libdfui.a
.else
LIBDFUI=${.CURDIR}/../libdfui/libdfui.a
.endif

.if exists(${.OBJDIR}/../libinstaller)
LIBINSTALLER=${.OBJDIR}/../libinstaller/libinstaller.a
.else
LIBINSTALLER=${.CURDIR}/../libinstaller/libinstaller.a
.endif

DPADD+=	${LIBINSTALLER} ${LIBDFUI} ${LIBAURA} ${LIBPANEL} ${LIBNCURSES}
LDADD+=	${LIBINSTALLER} ${LIBDFUI} ${LIBAURA} -lprivate_panel -lprivate_ncurses
CFLAGS+=	-I${_SHLIBDIRPREFIX}/usr/include/priv/ncurses
LDFLAGS+=	${PRIVATELIB_LDFLAGS}

.include <bsd.prog.mk>
