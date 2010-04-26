# $FreeBSD: src/tools/tools/net80211/w00t/Makefile.inc,v 1.2 2010/02/25 14:42:26 ru Exp $

W00T=	../libw00t
# NB: we get crc32 from -lz
DPADD=	${W00T}/libw00t.a ${LIBCRYPTO} ${LIBZ}
LDFLAGS= -L${W00T}
LDADD=	-lw00t  -lcrypto -lz

BINDIR=	/usr/local/bin
CFLAGS=	-g -I${W00T}

NOMAN=
