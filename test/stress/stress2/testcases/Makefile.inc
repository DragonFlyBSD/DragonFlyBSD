# $FreeBSD$

NOOBJ=
LDADD+= -L../../lib -lstress -lutil
DPADD+= ../../lib/libstress.a
CFLAGS =-g -Wall -I../../include -I../include
NOMAN=
