#
#

ATH_RATE?=	sample		# tx rate control algorithm

KMOD=	ath_hal

HALCONTRIB= ${.CURDIR}/../../../../contrib/dev/ath/ath_hal

SRCS=	ah_regdomain.c
SRCS+=	ah_eeprom_v3.c ah_osdep.c ah.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_wlan.h opt_inet.h opt_ath.h opt_ah.h

#
# AR5210 support; these are first generation 11a-only devices.
#
.PATH:	${.CURDIR}/ar5210
SRCS+=	ah_eeprom_v1.c \
	ar5210_attach.c ar5210_beacon.c ar5210_interrupts.c \
	ar5210_keycache.c ar5210_misc.c ar5210_phy.c ar5210_power.c \
	ar5210_recv.c ar5210_reset.c ar5210_xmit.c

#
# AR5211 support; these are second generation 11b/g/a devices
# (but 11g was OFDM only and is not supported).
#
.PATH:	${.CURDIR}/ar5211
SRCS+=	ar5211_attach.c ar5211_beacon.c ar5211_interrupts.c \
	ar5211_keycache.c ar5211_misc.c	ar5211_phy.c ar5211_power.c \
	ar5211_recv.c ar5211_reset.c ar5211_xmit.c

#
# AR5212 support; this covers all other pci/cardbus legacy parts.
#
.PATH:	${.CURDIR}/ar5212
SRCS+=	ar5212_ani.c ar5212_attach.c ar5212_beacon.c ar5212_eeprom.c \
	ar5212_gpio.c ar5212_interrupts.c ar5212_keycache.c ar5212_misc.c \
	ar5212_phy.c ar5212_power.c ar5212_recv.c ar5212_reset.c \
	ar5212_rfgain.c ar5212_xmit.c
# RF backends
SRCS+=	ar5111.c
SRCS+=	ar5112.c
SRCS+=	ar2413.c
SRCS+=	ar2425.c
SRCS+=	ar5413.c

#
# AR5416, AR9130, AR9160, AR9220, AR9280, AR9285, AR9287 support.

# Note enabling this support requires defining AH_SUPPORT_AR5416
# in opt_ah.h so the 11n tx/rx descriptor format is handled.
#
# NB: 9160 depends on 5416 but 5416 does not require 9160
#
# + 5416 (Owl)
.PATH:	${.CURDIR}/ar5416
SRCS+=	ah_eeprom_v14.c ah_eeprom_v4k.c \
	ar5416_ani.c ar5416_attach.c ar5416_beacon.c ar5416_btcoex.c \
	ar5416_cal.c ar5416_cal_iq.c ar5416_cal_adcgain.c ar5416_cal_adcdc.c \
	ar5416_eeprom.c ar5416_gpio.c ar5416_interrupts.c ar5416_keycache.c \
	ar5416_misc.c ar5416_phy.c ar5416_power.c ar5416_radar.c ar5416_spectral.c \
	ar5416_recv.c ar5416_reset.c ar5416_xmit.c

# RF backend for 5416, 9130 and 9160
SRCS+=	ar2133.c

# + AR9160 (Sowl)
.PATH:	${.CURDIR}/ar9001
SRCS+=	ar9160_attach.c

# + AR9130 - (Sowl) - Embedded (AR913x SoC)
#
# This requires AH_SUPPORT_AR9130 in order to function as some
# register values have shifted for this chipset. Definig this however
# (currently) breaks non-AR9130 chipsets - since this is an embedded
# chipset and no other radios are glued to it, this shouldn't pose a
# problem.
SRCS+=	ar9130_attach.c ar9130_eeprom.c ar9130_phy.c

# AR9002 series chips
# + AR9220/AR9280 - Merlin
.PATH:	${.CURDIR}/ar9002
SRCS+=	ar9280.c ar9280_attach.c ar9280_olc.c

# + AR9285 - Kite
SRCS+=	ar9285.c ar9285_reset.c ar9285_attach.c ar9285_cal.c ar9285_phy.c
SRCS+=	ar9285_diversity.c ar9285_btcoex.c

# + AR9287 - Kiwi
SRCS+=  ah_eeprom_9287.c
.PATH:  ${.CURDIR}/ar9002
SRCS+=  ar9287.c ar9287_reset.c ar9287_attach.c ar9287_cal.c ar9287_olc.c

# + AR9300 HAL
.PATH:  ${HALCONTRIB}/ar9300
SRCS+= ar9300_interrupts.c ar9300_radar.c ar9300_ani.c ar9300_keycache.c
SRCS+= ar9300_radio.c ar9300_xmit.c ar9300_attach.c ar9300_mci.c ar9300_stub.c
SRCS+= ar9300_xmit_ds.c ar9300_beacon.c ar9300_misc.c ar9300_recv.c
SRCS+= ar9300_stub_funcs.c ar9300_eeprom.c ar9300_paprd.c ar9300_recv_ds.c
SRCS+= ar9300_freebsd.c ar9300_phy.c ar9300_reset.c ar9300_gpio.c
SRCS+= ar9300_power.c ar9300_timer.c

CFLAGS+=  -I. -I${HAL} -I${HALCONTRIB}

.include <bsd.kmod.mk>
