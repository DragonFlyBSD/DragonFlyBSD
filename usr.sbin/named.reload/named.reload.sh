#!/bin/sh -
#
#	from named.reload	5.2 (Berkeley) 6/27/89
# $FreeBSD: src/usr.sbin/named.reload/named.reload.sh,v 1.2 1999/08/28 01:17:23 peter Exp $
# $DragonFly: src/usr.sbin/named.reload/named.reload.sh,v 1.3 2004/05/27 18:15:43 dillon Exp $
#

exec %DESTSBIN%/%INDOT%rndc reload
