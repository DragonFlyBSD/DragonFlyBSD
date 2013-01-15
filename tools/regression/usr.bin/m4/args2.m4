dnl $FreeBSD: src/tools/regression/usr.bin/m4/args2.m4,v 1.2 2012/11/17 01:53:58 svnexp Exp $
dnl $OpenBSD: src/regress/usr.bin/m4/args2.m4,v 1.1 2008/08/16 09:57:12 espie Exp $
dnl Preserving spaces within nested parentheses
define(`foo',`$1')dnl
foo((	  check for embedded spaces))
