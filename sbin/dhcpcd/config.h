/* dragonfly */
#ifndef	SYSCONFDIR
#define	SYSCONFDIR		"/etc"
#define	SBINDIR			"/sbin"
#define	LIBDIR			"/lib"
#define	LIBEXECDIR		"/usr/libexec"
#define	DBDIR			"/var/db/dhcpcd"
#define	RUNDIR			"/var/run/dhcpcd"
#endif
#ifndef PRIVSEP_USER
#define PRIVSEP_USER		 "_dhcp"
#endif
#define	HAVE_IFAM_ADDRFLAGS
#define	HAVE_IFADDRS_ADDRFLAGS
#define	HAVE_OPEN_MEMSTREAM
#include			"compat/pidfile.h"
#include			"compat/strtoi.h"
#include			"compat/consttime_memequal.h"
#define	TAILQ_FOREACH_SAFE	TAILQ_FOREACH_MUTABLE
#define	HAVE_SYS_QUEUE_H
#define	RBTEST
#include			"compat/rbtree.h"
#define	HAVE_REALLOCARRAY
#define	HAVE_PPOLL
#ifdef	USE_PRIVATECRYPTO
#define	HAVE_MD5_H
#define	SHA2_H			<openssl/sha.h>
#else
#include			"compat/crypt/md5.h"
#include			"compat/crypt/sha256.h"
#endif
#include			"compat/crypt/hmac.h"
