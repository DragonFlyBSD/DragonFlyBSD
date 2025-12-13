#
#

_FIRM=  ${IMG}.fw

CLEANFILES+=    ${_FIRM}

FIRMWS= ${_FIRM}:${KMOD}

#
# Note that a license ack is not needed for mtw.
#
#FIRMWARE_LICENSE=

${_FIRM}: ${.CURDIR}/../../../../../contrib/dev/mtw/${_FIRM}.uu
	uudecode -p $? > ${.TARGET}
