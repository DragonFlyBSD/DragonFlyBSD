SUBDIR=	cam firewire iicbus mmc pccard ppbus smbus

.if defined(WANT_USB4BSD)
SUBDIR+=u4b
.else
SUBDIR+=usb
.endif

.include <bsd.subdir.mk>
