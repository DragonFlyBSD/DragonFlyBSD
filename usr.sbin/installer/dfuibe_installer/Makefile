#
# $DragonFly: src/usr.sbin/installer/dfuibe_installer/Makefile,v 1.1 2008/03/12 01:34:49 dave Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src

PATCHES= 

.PATH: ${BSDINS_SRC}/backend/installer

BINDIR=	/usr/sbin

PROG=	dfuibe_installer
MAN=	 
SRCS=	flow.c fn_diagnostic.c fn_install.c fn_zonetab.c \
        fn_configure.c fn_disk.c fn_subpart.c main.c \
	aura.h dfui.h installer.h ${PATCHES}
BINOWN=	root
BINGRP=	wheel

CFLAGS+= -I${BSDINS_SRC}/backend/installer -I.

# This madness is because the installer sources reference aura includes
# as <aura/mem.h> rather than <libaura/mem.h>
aura.h:
	${LN} -sf ${BSDINS_SRC}/lib/libaura aura
	/usr/bin/touch aura.h

dfui.h:
	${LN} -sf ${BSDINS_SRC}/lib/libdfui dfui
	/usr/bin/touch dfui.h

installer.h:
	${LN} -sf ${BSDINS_SRC}/lib/libinstaller installer
	/usr/bin/touch installer.h

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

.include <bsd.prog.mk>
