# $DragonFly: src/lib/libcaps/Makefile,v 1.1 2003/11/24 21:15:58 dillon Exp $
#
LIB=lwkt
SHLIB_MAJOR= 1
SHLIB_MINOR= 0
AINC= -I${.CURDIR}/${MACHINE_ARCH} 
CFLAGS+= -I${.CURDIR}/..
INSTALL_PIC_ARCHIVE=	yes

SRCS=	lwkt_msgport.c lwkt_thread.c slaballoc.c globaldata.c \
	caps_client.c caps_misc.c caps_service.c

.PATH:	${.CURDIR}/../../sys/kern

.include <bsd.lib.mk>
