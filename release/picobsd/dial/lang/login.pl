#!/bin/sh
# $FreeBSD: src/release/picobsd/dial/lang/login.pl,v 1.3 1999/08/28 01:33:21 peter Exp $
# $DragonFly: src/release/picobsd/dial/lang/Attic/login.pl,v 1.2 2003/06/17 04:27:20 dillon Exp $

if [ "$2" != "root" ]
then
	exit
fi
cat /etc/motd
LANG=pl; export LANG
HOME=/root exec -sh
