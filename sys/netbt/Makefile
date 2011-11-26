# $DragonFly: src/sys/netbt/Makefile,v 1.1 2007/12/30 20:02:56 hasso Exp $

KMOD	= netbt
SRCS	= sco_socket.c sco_upper.c bt_input.c bt_proto.c hci_event.c hci_ioctl.c
SRCS	+= hci_link.c hci_misc.c hci_socket.c hci_unit.c l2cap_lower.c
SRCS	+= l2cap_misc.c l2cap_signal.c l2cap_socket.c l2cap_upper.c
SRCS	+= rfcomm_dlc.c rfcomm_session.c rfcomm_socket.c rfcomm_upper.c
SRCS	+= bluetooth.h hci.h l2cap.h rfcomm.h sco.h 
SRCS	+= opt_usb.h device_if.h bus_if.h

.include <bsd.kmod.mk>


