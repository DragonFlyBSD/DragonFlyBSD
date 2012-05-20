KMOD	= ig_hal
SRCS	= device_if.h pci_if.h bus_if.h
SRCS	+= e1000_osdep.c
SRCS	+= e1000_api.c e1000_mac.c e1000_manage.c e1000_nvm.c e1000_phy.c \
	   e1000_mbx.c
SRCS	+= e1000_80003es2lan.c e1000_82540.c e1000_82541.c e1000_82542.c \
	   e1000_82543.c e1000_82571.c e1000_ich8lan.c e1000_82575.c e1000_vf.c

.include <bsd.kmod.mk>
