#
#

KMOD=	if_ath

SRCS=	if_ath.c if_ath_keycache.c if_ath_spectral.c
SRCS+=	if_ath_ahb.c if_ath_led.c if_ath_sysctl.c
SRCS+=	if_ath_alq.c if_ath_lna_div.c if_ath_tdma.c
SRCS+=	if_ath_beacon.c if_ath_pci.c if_ath_tx.c
SRCS+=	if_ath_btcoex.c if_ath_rx.c if_ath_tx_edma.c
SRCS+=	if_ath_debug.c if_ath_rx_edma.c if_ath_tx_ht.c

SRCS+=	device_if.h bus_if.h pci_if.h
SRCS+=	opt_ath.h opt_ah.h opt_wlan.h opt_inet.h opt_ktr.h

.include <bsd.kmod.mk>
