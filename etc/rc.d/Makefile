# $NetBSD: Makefile,v 1.16 2001/01/14 15:37:22 minoura Exp $
# $FreeBSD: src/etc/rc.d/Makefile,v 1.20 2003/06/29 05:15:57 mtm Exp $

.include <bsd.own.mk>

# note: bgfsk and lomac left out (from 5.0)
#
FILES=	DAEMON LOGIN NETWORKING SERVERS abi accounting addswap adjkerntz \
	amd apm apmd \
	battd bootconf bootparams btconfig bthcid ccd cleanvar cryptdisks \
	cleartmp cron cryptdisks devd devfs dhclient diskless dmesg dumpon \
	fixbootfile fsck ftpd hostapd hostname hotplugd \
	inetd initdiskless initrandom ip6fw ipfw ipsec \
	jail keyserv \
	ldconfig local localdaemons lockd lpd lvm \
	mixer motd mountcritlocal mountcritremote \
	mountd moused mroute6d mrouted msgs \
	named netif netoptions newsyslog \
	network_ipv6 nfsclient nfsd nfsserver nisdomain nscd nsswitch \
	dntpd othermta pf pflog powerd ppp ppp-user pppoed pwcheck \
	quota random rarpd rcconf resident rndcontrol root route6d routed \
	routing rpcbind rtadvd rtsold rwho sysdb savecore sdpd securelevel \
	sendmail sensorsd serial sppp sshd statd swap1 syscons sysctl syslogd sysvipcd \
	timed ttys udevd udevd_early usbd \
	varsym vfs_quota vinum virecover vkernel vknetd \
	watchdogd wpa_supplicant \
	ypbind yppasswdd ypserv ypset ypupdated ypxfrd

FILESDIR=	/etc/rc.d
FILESMODE=	${BINMODE}

NOOBJ=

.include <bsd.prog.mk>
