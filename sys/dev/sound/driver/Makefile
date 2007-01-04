# $FreeBSD: src/sys/modules/sound/driver/Makefile,v 1.16.2.1 2005/12/30 19:55:54 netchild Exp $
# $DragonFly: src/sys/dev/sound/driver/Makefile,v 1.3 2007/01/04 21:47:00 corecode Exp $

.if ${MACHINE_ARCH} == "sparc64"
SUBDIR  = audiocs es137x
.else
SUBDIR  = als4000 ad1816 atiixp cmi cs4281 csa ds1 emu10k1 es137x ess
SUBDIR += fm801 ich maestro maestro3 mss neomagic sb16 sb8 sbc solo
SUBDIR += t4dwave via8233 via82c686 vibes
SUBDIR += hda
SUBDIR += driver
SUBDIR += uaudio
.endif

.include <bsd.subdir.mk>
