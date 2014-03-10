# $FreeBSD: src/sys/modules/sound/driver/Makefile,v 1.16.2.2 2007/05/13 21:11:40 ariff Exp $

SUBDIR  = als4000 atiixp cmi cs4281 csa ds1 emu10k1 envy24
SUBDIR += envy24ht es137x fm801 hda ich maestro maestro3 neomagic
SUBDIR += solo spicds t4dwave via8233 via82c686 vibes
SUBDIR += driver

.if defined(WANT_OLDUSB)
SUBDIR += uaudio
.endif

.include <bsd.subdir.mk>
