#$FreeBSD: head/rescue/rescue/Makefile 252356 2013-06-28 21:00:08Z davide $
#	@(#)Makefile	8.1 (Berkeley) 6/2/93

NO_MAN=	YES

.include <bsd.own.mk>

PROG=	sbin.libcrypto
BINDIR=	/usr/share/initrd/sbin

CRUNCH_SRCDIRS+= sbin
CRUNCH_PROGS_sbin= tcplay

CRUNCH_SRCDIRS+= gnu/sbin
CRUNCH_PROGS_gnu/sbin= cryptsetup

CRUNCH_LIBS+= -ldm -lprop -lutil

CRUNCH_SRCDIRS+= usr.sbin/802_11
CRUNCH_PROGS_usr.sbin/802_11= wpa_supplicant
CRUNCH_LIBS+= -lpcap -lssl -lcrypto

.include <bsd.crunchgen.mk>
.include <bsd.prog.mk>
