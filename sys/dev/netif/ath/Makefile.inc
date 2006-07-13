# $DragonFly: src/sys/dev/netif/ath/Makefile.inc,v 1.1 2006/07/13 09:15:22 sephe Exp $

HAL	= ${.CURDIR}/../../../../contrib/dev/ath
CONTRIBDIR = ${HAL}/dragonfly
CFLAGS	+= -I${CONTRIBDIR}
