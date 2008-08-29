# $DragonFly: src/usr.sbin/installer/libinstaller/Makefile,v 1.5 2008/08/29 22:51:56 swildner Exp $

BSDINS_DIR=${.CURDIR}/../../../contrib/bsdinstaller-1.1.6
BSDINS_SRC=${BSDINS_DIR}/src

CONTRIBDIR=	${BSDINS_SRC}/lib/libinstaller
.PATH:	${CONTRIBDIR}

CFLAGS+=	-I${BSDINS_SRC}/lib

LIB=    installer

SRCS=	commands.c confed.c diskutil.c functions.c mount.c package.c survey.c \
	uiutil.c 

CLEANFILES+=	aura dfui
INTERNALLIB=	true

DPADD+= ${.CURDIR}/../libaura/libaura.a  ${.CURDIR}/../libdfui/libdfui.a
LDADD+= -laura -ldfui

.include <bsd.lib.mk>
