# $DragonFly: src/usr.sbin/installer/libinstaller/Makefile,v 1.3 2008/07/14 23:57:10 swildner Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src

CONTRIBDIR=	${BSDINS_SRC}/lib/libinstaller
.PATH:	${CONTRIBDIR}

PATCHES=	functions.c.patch survey.c.patch

CFLAGS+=	-I${BSDINS_SRC}/lib/libdfui -I${CONTRIBDIR} -I. -g

LIB=    installer

SRCS+=	commands.c confed.c diskutil.c functions.c mount.c package.c survey.c uiutil.c 
SRCS+=	aura.h dfui.h
SRCS+=	${PATCHES}

INCS+=	commands.h confed.h diskutil.h functions.h package.h sysids.h uiutil.h

# This madness is because the installer sources reference aura includes
# as <aura/mem.h> rather than <libaura/mem.h>
aura.h:
	${LN} -sf ${BSDINS_SRC}/lib/libaura aura
	/usr/bin/touch aura.h

dfui.h:
	${LN} -sf ${BSDINS_SRC}/lib/libdfui dfui
	/usr/bin/touch dfui.h

CLEANFILES+=	aura.h dfui.h aura dfui
INTERNALLIB=            true

DPADD+= ${.CURDIR}/../libaura/libaura.a  ${.CURDIR}/../libdfui/libdfui.a
LDADD+= -laura -ldfui

.include <bsd.lib.mk>

