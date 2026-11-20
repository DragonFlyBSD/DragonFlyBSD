.include <bsd.dragonfly.mk>

_FIRM=  ${IMG}.fw

CLEANFILES+=    ${_FIRM}

FIRMWS= ${_FIRM}:${KMOD}

# Note that a license ack is not needed for iwm.
#FIRMWARE_LICENSE=

${_FIRM}: ${DF.SRCTOP}/sys/contrib/dev/iwm/${_FIRM}.uu
	${NORMAL_FW}
