dnl $FreeBSD: src/tools/regression/usr.bin/m4/gnupatterns.m4,v 1.2 2012/11/17 01:53:59 svnexp Exp $
patsubst(`string with a + to replace with a minus', `+', `minus')
patsubst(`string with aaaaa to replace with a b', `a+', `b')
patsubst(`+string with a starting + to replace with a minus', `^+', `minus')
