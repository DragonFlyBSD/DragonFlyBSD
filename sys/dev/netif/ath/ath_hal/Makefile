#
#

ATH_RATE?=	sample		# tx rate control algorithm

KMOD=	ath_hal

.PATH: ar5211 ar5416 ar5312 ar5212 ar9003 ar9002 ar9001 ar5210

SRCS=	ah_eeprom_9287.c ah_eeprom_v4k.c ah_regdomain.c \
SRCS+=	ah_eeprom_v1.c ah_eeprom_v3.c ah_osdep.c ah.c ah_eeprom_v14.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_inet.h opt_ath.h opt_ah.h

# AR5211
#
SRCS+=	ar5211_attach.c ar5211_power.c ar5211_keycache.c ar5211_beacon.c
SRCS+=	ar5211_recv.c ar5211_misc.c ar5211_xmit.c ar5211_reset.c ar5211_phy.c
SRCS+=	ar5211_interrupts.c

# AR5416
#
SRCS+=	ar5416_power.c ar5416_radar.c ar5416_cal_adcgain.c ar5416_ani.c
SRCS+=	ar5416_attach.c ar5416_btcoex.c ar5416_beacon.c ar5416_spectral.c
SRCS+=	ar5416_keycache.c ar5416_cal.c ar5416_recv.c ar5416_gpio.c
SRCS+=	ar5416_eeprom.c ar5416_reset.c ar2133.c ar5416_cal_adcdc.c
SRCS+=	ar5416_interrupts.c ar5416_xmit.c ar5416_misc.c ar5416_phy.c
SRCS+=	ar5416_cal_iq.c

# AR5312
#
SRCS+=	ar5312_misc.c ar5312_interrupts.c ar5312_reset.c ar5312_eeprom.c
SRCS+=	ar5312_gpio.c ar5315_gpio.c ar5312_power.c ar5312_attach.c

# AR5212
#
SRCS+=	ar5212_rfgain.c ar5212_interrupts.c ar5212_ani.c ar2316.c
SRCS+=	ar2317.c ar2413.c ar5413.c ar5212_power.c ar2425.c ar5212_recv.c
SRCS+=	ar5212_gpio.c ar5212_beacon.c ar5212_misc.c ar5212_xmit.c
SRCS+=	ar5212_eeprom.c ar5212_keycache.c ar5212_phy.c ar5212_attach.c
SRCS+=	ar5212_reset.c ar5111.c ar5112.c

# AR9002
#

SRCS+=	ar9280_olc.c ar9280_attach.c ar9285_diversity.c ar9285_phy.c
SRCS+=	ar9287.c ar9285.c ar9280.c ar9287_reset.c ar9287_olc.c ar9285_cal.c
SRCS+=	ar9287_attach.c ar9285_reset.c ar9285_attach.c ar9285_btcoex.c
SRCS+=	ar9287_cal.c

# AR9001
#
SRCS+=	ar9130_phy.c ar9160_attach.c ar9130_attach.c ar9130_eeprom.c

# AR5210
#
SRCS+=	ar5210_misc.c ar5210_xmit.c ar5210_power.c ar5210_keycache.c
SRCS+=	ar5210_beacon.c ar5210_reset.c ar5210_interrupts.c ar5210_attach.c
SRCS+=	ar5210_recv.c ar5210_phy.c

CFLAGS+=  -I. -I${HAL}

.include <bsd.kmod.mk>
