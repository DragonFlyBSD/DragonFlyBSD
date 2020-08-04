# $FreeBSD: head/rescue/rescue/Makefile 252356 2013-06-28 21:00:08Z davide $
#	@(#)Makefile	8.1 (Berkeley) 6/2/93

# To include '../Makefine.inc' for use in <bsd.crunchgen.mk>
.include <bsd.init.mk>

PROG= rescue.libcrypto

# Programs from 'bin'
#
CRUNCH_SRCDIRS+= bin
CRUNCH_PATH_bin= ${.CURDIR}/../..
CRUNCH_PROGS_bin= ed

CRUNCH_ALIAS_ed= red

# Programs from 'usr.bin'
#
CRUNCH_SRCDIRS+= usr.bin
CRUNCH_PATH_usr.bin= ${.CURDIR}/../..
CRUNCH_PROGS_usr.bin=	\
	cpio		\
	fetch		\
	nc		\
	scp		\
	ssh		\
	tar		\
	telnet

CRUNCH_ALIAS_ssh= slogin

# telnet requires the 'libtelnet.a' internal library
CRUNCH_INTLIB_telnet= ${.CURDIR}/../../lib/libtelnet/libtelnet.a

# Programs from 'sbin'
#
CRUNCH_SRCDIRS+= sbin
CRUNCH_PATH_sbin= ${.CURDIR}/../..
CRUNCH_PROGS_sbin=	\
	md5		\
	hammer		\
	hammer2		\
	ping6		\
	tcplay

CRUNCH_ALIAS_md5= rmd160 sha1 sha256 sha512

# Programs from 'usr.sbin'
#
CRUNCH_SRCDIRS+= usr.sbin/802_11
CRUNCH_PATH_usr.sbin/802_11= ${.CURDIR}/../..
CRUNCH_PROGS_usr.sbin/802_11= wpa_supplicant

# Programs from 'gnu/sbin'
#
CRUNCH_SRCDIRS+= gnu/sbin
CRUNCH_PATH_gnu/sbin= ${.CURDIR}/../..
CRUNCH_PROGS_gnu/sbin= cryptsetup

CRUNCH_LIBS+= -ldm -lprop  # tcplay, cryptsetup
CRUNCH_LIBS+= -lpcap  # wpa_supplicant
CRUNCH_LIBS+= -lhammer  # hammer
CRUNCH_LIBS+= -ldmsg  # hammer2
CRUNCH_LIBS+= -lfetch  # fetch
CRUNCH_LIBS+= -larchive  # cpio, tar
CRUNCH_LIBS+= -llzma  # cpio, tar
CRUNCH_LIBS+= -lbz2  # cpio, tar
CRUNCH_LIBS+= -lprivate_ncurses -lcrypt  # telnet
CRUNCH_LIBS+= -lprivate_ssl  # fetch, wpa_supplicant
CRUNCH_LIBS+= -lprivate_ssh  # ssh
CRUNCH_LIBS+= -lprivate_crypto # cpio, hammer2, md5, ping6, tar
CRUNCH_LIBS+= -lprivate_ldns  # ssh
CRUNCH_LIBS+= -lutil
CRUNCH_LIBS+= -lz  # cpio, ssh, tar
CRUNCH_LIBS+= -lm  # hammer, hammer2, ping6
CRUNCH_LIBS+= -lpthread  # hammer2
CRUNCH_LINKOPTS+= ${PRIVATELIB_LDFLAGS}

.include <bsd.crunchgen.mk>
.include <bsd.prog.mk>
