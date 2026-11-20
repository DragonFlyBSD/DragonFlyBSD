.include <bsd.dragonfly.mk>

_FIRM=  ${IMG}.fw

CLEANFILES+=    ${_FIRM}

FIRMWS= ${_FIRM}:${KMOD}

# Note that a license ack is not needed for mtw.
#FIRMWARE_LICENSE=

${_FIRM}: ${DF.SRCTOP}/sys/contrib/dev/mtw/${_FIRM}.uu
	${NORMAL_FW}
