# $NetBSD: Makefile,v 1.16 2001/01/14 15:37:22 minoura Exp $
# $FreeBSD: src/etc/rc.d/Makefile,v 1.20 2003/06/29 05:15:57 mtm Exp $
# $DragonFly: src/etc/rc.d/Makefile,v 1.26 2008/01/20 11:23:35 swildner Exp $

.include <bsd.own.mk>

# note: bgfsk, devd, devfs, and lomac left out (from 5.0)
#
FILES=	DAEMON LOGIN NETWORKING SERVERS abi accounting addswap adjkerntz \
	amd apm apmd atm1 atm2.sh atm3.sh \
	battd bootconf bootparams btconfig ccd cleanvar \
	cleartmp cron dhclient dhcpd dhcrelay diskless dmesg dumpon \
	fsck ftpd hostapd hostname \
	inetd initdiskless initrandom ip6fw ipfilter ipfs ipfw ipmon \
	ipnat ipsec ipxrouted isdnd jail \
	kadmind kerberos keyserv kpasswdd \
	ldconfig local localdaemons lpd \
	mixer motd mountcritlocal mountcritremote \
	mountd moused mroute6d mrouted msgs \
	named netif netoptions newsyslog \
	network_ipv6 nfsclient nfsd nfslocking nfsserver nisdomain ntpd \
	dntpd othermta pf pflog ppp ppp-user pppoed pwcheck \
	quota random rarpd rcconf.sh resident rndcontrol root route6d routed \
	routing rpcbind rtadvd rtsold rwho sysdb savecore sdpd securelevel \
	sendmail sensorsd serial sppp sshd swap1 syscons sysctl syslogd \
	timed ttys usbd varsym vinum virecover watchdogd wpa_supplicant \
	ypbind yppasswdd ypserv ypset ypupdated ypxfrd
FILESDIR=	/etc/rc.d
FILESMODE=	${BINMODE}

NOPROG= noprog

.include <bsd.prog.mk>
