#! /bin/sh
# ex:ts=8

# $FreeBSD: src/usr.bin/less/lesspipe.sh,v 1.1.2.1 2003/03/16 04:48:44 obrien Exp $
# $DragonFly: src/usr.bin/less/lesspipe.sh,v 1.2 2003/06/17 04:29:27 dillon Exp $

case "$1" in
	*.Z)	uncompress -c $1	2>/dev/null
		;;
	*.gz)	gzip -d -c $1		2>/dev/null
		;;
	*.bz2)	bzip2 -d -c $1		2>/dev/null
		;;
esac
