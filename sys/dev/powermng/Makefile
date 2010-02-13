# $DragonFly: src/sys/dev/powermng/Makefile,v 1.2 2007/10/02 13:16:42 hasso Exp $
#

SUBDIR=	coretemp kate km
.if ${MACHINE_ARCH} == "i386"
SUBDIR+= powernow
.endif

.include <bsd.subdir.mk>
