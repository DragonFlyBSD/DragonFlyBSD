#!/bin/sh

# $FreeBSD: src/sys/modules/linux/linux.sh,v 1.8 1999/12/13 08:38:22 cracauer Exp $
# $DragonFly: src/sys/emulation/linux/Attic/linux.sh,v 1.2 2003/06/17 04:28:44 dillon Exp $

FOUND=`kldstat -v | egrep 'linux(aout|elf)'`

exitcode=0

if [ "x$FOUND" != x ] ; then           
	echo Linux driver already loaded
	exitcode=1
else    
	kldload linux                                                
	exitcode=$?
fi

if [ -f /compat/linux/sbin/ldconfig ] ; then
	/compat/linux/sbin/ldconfig
fi

exit $exitcode
