# $NetBSD: Makefile,v 1.3 2007/05/28 12:06:41 tls Exp $
# $DragonFly: src/usr.sbin/sdpd/Makefile,v 1.1 2008/01/06 21:51:30 hasso Exp $

PROG=		sdpd
MAN=		sdpd.8
SRCS=		bgd.c dun.c ftrn.c hf.c hset.c irmc.c irmc_command.c lan.c \
		log.c main.c opush.c profile.c provider.c sar.c scr.c \
		sd.c server.c sp.c srr.c ssar.c ssr.c sur.c uuid.c
WARNS?=		1

CFLAGS+=	-I${.CURDIR}/../../sys

.include <bsd.prog.mk>
