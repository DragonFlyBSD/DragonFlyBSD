#
# $DragonFly: src/usr.sbin/installer/dfuibe_installer/Makefile,v 1.5 2008/08/29 22:51:56 swildner Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src

CONTRIBDIR=${BSDINS_SRC}/backend/installer
.PATH: ${CONTRIBDIR}

PROG=	dfuibe_installer
NOMAN=
SRCS=	flow.c fn_diagnostic.c fn_install.c fn_zonetab.c \
        fn_configure.c fn_disk.c fn_subpart_hammer.c fn_subpart.c main.c

CFLAGS+= -I${BSDINS_SRC}/lib

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
