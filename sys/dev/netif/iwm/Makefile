#
#
KMOD    = if_iwm
SRCS    = if_iwm.c if_iwm_pcie_trans.c if_iwm_power.c if_iwm_util.c
SRCS	+= if_iwm_binding.c if_iwm_phy_ctxt.c if_iwm_scan.c if_iwm_led.c
SRCS	+= if_iwm_mac_ctxt.c if_iwm_phy_db.c if_iwm_time_event.c
SRCS	+= if_iwm_notif_wait.c if_iwm_7000.c if_iwm_8000.c if_iwm_fw.c
SRCS	+= if_iwm_sta.c if_iwm_sf.c
SRCS	+= device_if.h bus_if.h pci_if.h opt_iwm.h opt_wlan.h

SUBDIR	= fw

CFLAGS += -DIWM_DEBUG

.include <bsd.kmod.mk>
