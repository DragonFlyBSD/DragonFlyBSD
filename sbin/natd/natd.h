/*
 * natd - Network Address Translation Daemon for FreeBSD.
 *
 * This software is provided free of charge, with no
 * warranty of any kind, either expressed or implied.
 * Use at your own risk.
 *
 * You may copy, modify and distribute this software (natd.h) freely.
 *
 * Ari Suutari <suutari@iki.fi>
 *
 * $FreeBSD: src/sbin/natd/natd.h,v 1.4 1999/08/28 00:13:46 peter Exp $
 */

#define PIDFILE	"/var/run/natd.pid"
#define	INPUT		1
#define	OUTPUT		2
#define	DONT_KNOW	3

#define EXIT_DELAY	10000
#define MAX_EXIT_DELAY	999999

extern void Quit(const char *) __dead2;
extern void Warn(const char *);
extern int SendNeedFragIcmp(int, struct ip *, int);


