#$FreeBSD: head/rescue/rescue/Makefile 252356 2013-06-28 21:00:08Z davide $
#	@(#)Makefile	8.1 (Berkeley) 6/2/93

NO_MAN=	YES

.include <bsd.own.mk>

PROG=	rescue
BINDIR=	/usr/share/initrd/bin
SCRIPTS=dowpa

###################################################################
# Programs from stock /bin
#
# WARNING: Changing this list may require adjusting
# /usr/include/paths.h as well!  You were warned!
#
CRUNCH_SRCDIRS+= bin
CRUNCH_PROGS_bin=	\
	cat		\
	chmod		\
	cp		\
	cpdup		\
	dd		\
	df		\
	echo		\
	kill		\
	ln		\
	ls		\
	mined		\
	mkdir		\
	mv		\
	ps		\
	pwd		\
	rm		\
	rmdir		\
	sh		\
	sleep		\
	sync		\
	test

CRUNCH_LIBS+= -lcrypt -ledit -lkvm -ll -lmd -ltermcap -lutil
CRUNCH_BUILDTOOLS+= bin/sh

# Additional options for specific programs
CRUNCH_ALIAS_test= [

CRUNCH_ALIAS_sh= -sh
# The -sh alias shouldn't appear in /rescue as a hard link
CRUNCH_SUPPRESS_LINK_-sh= 1

CRUNCH_ALIAS_ln= link
CRUNCH_ALIAS_rm= unlink


CRUNCH_SRCDIRS+= usr.bin
CRUNCH_PROGS_usr.bin= du less
CRUNCH_ALIAS_less= more

CRUNCH_SRCDIRS+= secure/usr.bin
CRUNCH_PROGS_secure/usr.bin= ssh
CRUNCH_ALIAS_ssh= scp

CRUNCH_KEEP_ssh= roaming_write roaming_read add_recv_bytes

CRUNCH_LIBS+= -lssh -lcrypto -lz -lpthread

.include <bsd.crunchgen.mk>
.include <bsd.prog.mk>
