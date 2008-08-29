#
# $DragonFly: src/usr.sbin/installer/dfuife_curses/Makefile,v 1.3 2008/08/29 22:51:56 swildner Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src

.PATH: ${BSDINS_SRC}/frontends/ncurses

PROG=	dfuife_curses
NOMAN=
SRCS=	curses_bar.c curses_form.c curses_util.c curses_widget.c \
	curses_xlat.c main.c

CFLAGS+= -I${BSDINS_SRC}/lib

DPADD+= ${LIBPANEL} ${LIBNCURSES}
LDADD+= -lpanel -lncurses

CLEANFILES+=	aura dfui installer

.if exists(${.OBJDIR}/../libaura)
LIBAURADIR:=      ${.OBJDIR}/../libaura
.else
LIBAURADIR!=      cd ${.CURDIR}/../libaura; make -V .OBJDIR
.endif
LIBAURA:=         ${LIBAURADIR}/libaura.a

.if exists(${.OBJDIR}/../libdfui)
LIBDFUIDIR:=      ${.OBJDIR}/../libdfui
.else
LIBDFUIDIR!=      cd ${.CURDIR}/../libdfui; make -V .OBJDIR
.endif
LIBDFUI:=         ${LIBDFUIDIR}/libdfui.a

.if exists(${.OBJDIR}/../libinstaller)
LIBINSTALLERDIR:=      ${.OBJDIR}/../libinstaller
.else
LIBINSTALLERDIR!=      cd ${.CURDIR}/../libinstaller; make -V .OBJDIR
.endif
LIBINSTALLER:=         ${LIBINSTALLERDIR}/libinstaller.a

DPADD+=	${LIBAURA} ${LIBDFUI} ${LIBINSTALLER}
LDADD+=	-L${LIBAURADIR} -L${LIBDFUIDIR} -L${LIBINSTALLERDIR} -linstaller -ldfui -laura

.include "../../Makefile.inc"
.include <bsd.prog.mk>
