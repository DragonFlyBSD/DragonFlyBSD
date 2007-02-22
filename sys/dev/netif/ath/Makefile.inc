# $DragonFly: src/sys/dev/netif/ath/Makefile.inc,v 1.2 2007/02/22 05:17:09 sephe Exp $

HAL	= ${.CURDIR}/../../../../contrib/dev/ath
HALOSDEP= ${.CURDIR}/../hal
CFLAGS	+= -I${HALOSDEP}
