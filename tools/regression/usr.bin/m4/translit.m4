dnl $FreeBSD: src/tools/regression/usr.bin/m4/translit.m4,v 1.2 2012/11/17 01:53:59 svnexp Exp $
dnl $OpenBSD: src/regress/usr.bin/m4/translit.m4,v 1.1 2010/03/23 20:11:52 espie Exp $
dnl first one should match, not second one
translit(`onk*', `**', `p_')
