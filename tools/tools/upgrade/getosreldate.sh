#! /bin/sh
# $FreeBSD: src/tools/tools/upgrade/getosreldate.sh,v 1.2 1999/08/28 00:54:34 peter Exp $
# $DragonFly: src/tools/tools/upgrade/Attic/getosreldate.sh,v 1.2 2003/06/17 04:29:11 dillon Exp $

RELDATE=`sysctl -n kern.osreldate 2>/dev/null`
if [ "x$RELDATE" = x ]; then
  RELDATE=200000	# assume something really old
fi
echo $RELDATE
