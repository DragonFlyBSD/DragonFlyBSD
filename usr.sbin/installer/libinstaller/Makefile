# $DragonFly: src/usr.sbin/installer/libinstaller/Makefile,v 1.1 2008/03/12 01:34:49 dave Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src
.PATH:	${BSDINS_SRC}/lib/libinstaller

CFLAGS+=-I${BSDINS_SRC}/lib/libdfui -I.

LIB=    installer

SRCS+= commands.c confed.c diskutil.c functions.c mount.c package.c survey.c uiutil.c \
	aura.h dfui.h

# This madness is because the installer sources reference aura includes
# as <aura/mem.h> rather than <libaura/mem.h>
aura.h:
	${LN} -sf ${BSDINS_SRC}/lib/libaura aura
	/usr/bin/touch aura.h

dfui.h:
	${LN} -sf ${BSDINS_SRC}/lib/libdfui dfui
	/usr/bin/touch dfui.h

INTERNALLIB=            true

DPADD+= ${.CURDIR}/../libaura/libaura.a  ${.CURDIR}/../libdfui/libdfui.a
LDADD+= -laura -ldfui

.include <bsd.lib.mk>

