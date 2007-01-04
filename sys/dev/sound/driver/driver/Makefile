# $FreeBSD: src/sys/modules/sound/driver/driver/Makefile,v 1.4 2003/02/07 15:05:37 nyan Exp $
# $DragonFly: src/sys/dev/sound/driver/driver/Makefile,v 1.1 2007/01/04 21:47:01 corecode Exp $

.PATH: ${.CURDIR}/../../../../dev/sound

KMOD=	snd_driver
SRCS=	device_if.h bus_if.h
SRCS+=	driver.c

KMODDEPS=	snd_ad1816
KMODDEPS+=	snd_als4000
KMODDEPS+=	snd_atiixp
#KMODDEPS+=	snd_aureal
KMODDEPS+=	snd_cmi
KMODDEPS+=	snd_cs4281
KMODDEPS+=	snd_csa
KMODDEPS+=	snd_ds1
KMODDEPS+=	snd_emu10k1
KMODDEPS+=	snd_es137x
KMODDEPS+=	snd_ess
KMODDEPS+=	snd_fm801
KMODDEPS+=	snd_hda
KMODDEPS+=	snd_ich
KMODDEPS+=	snd_maestro
KMODDEPS+=	snd_maestro3
KMODDEPS+=	snd_mss
KMODDEPS+=	snd_neomagic
KMODDEPS+=	snd_sb16
KMODDEPS+=	snd_sb8
KMODDEPS+=	snd_sbc
KMODDEPS+=	snd_solo
KMODDEPS+=	snd_t4dwave
KMODDEPS+=	snd_via8233
KMODDEPS+=	snd_via82c686
KMODDEPS+=	snd_vibes

.include <bsd.kmod.mk>
