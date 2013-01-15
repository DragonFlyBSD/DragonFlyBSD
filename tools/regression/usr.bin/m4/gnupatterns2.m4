dnl $FreeBSD: src/tools/regression/usr.bin/m4/gnupatterns2.m4,v 1.2 2012/11/17 01:53:59 svnexp Exp $
define(`zoinx',dnl
`patsubst($1,\(\w+\)\(\W*\),\1 )')dnl
zoinx(acosl asinl atanl \
       cosl sinl tanl \
       coshl sinhl tanhl)
