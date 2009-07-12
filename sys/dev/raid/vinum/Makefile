# $FreeBSD: src/sys/modules/vinum/Makefile,v 1.17 2000/01/04 23:46:41 grog Exp $ 
# $DragonFly: src/sys/dev/raid/vinum/Makefile,v 1.5 2005/02/03 22:10:57 joerg Exp $ 

KMOD= vinum

SRCS= opt_vinum.h request.h vinum.c vinumconfig.c vinumdaemon.c \
	vinumext.h vinumhdr.h vinuminterrupt.c vinumio.c vinumio.h \
	vinumioctl.c vinumkw.h vinumlock.c vinummemory.c vinumparser.c \
	vinumraid5.c vinumrequest.c vinumrevive.c vinumstate.c \
	vinumstate.h vinumutil.c vinumvar.h

.include <bsd.kmod.mk>
