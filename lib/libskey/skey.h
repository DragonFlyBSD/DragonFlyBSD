/*
 * $DragonFly: src/lib/libskey/skey.h,v 1.3 2003/11/12 20:21:31 eirikn Exp $
 */

#ifndef _SKEY_H_
#define _SKEY_H_

#include <sys/cdefs.h>

/* Server-side data structure for reading keys file during login */
struct skey {
	FILE *keyfile;
	char buf[256];
	char *logname;
	int n;
	char *seed;
	char *val;
	long	recstart; /*needed so reread of buffer is efficient*/
};

#ifdef _SKEY_INTERNAL
/* Client-side structure for scanning data stream for challenge */
struct mc {
	char buf[256];
	int skip;
	int cnt;
};

#define atob8           _sk_atob8
#define btoa8           _sk_btoa8
#define btoe            _sk_btoe
#define etob            _sk_etob
#define f               _sk_f
#define htoi            _sk_htoi
#define keycrunch       _sk_keycrunch
#define put8            _sk_put8
#define readpass        _sk_readpass
#define rip             _sk_rip
#define sevenbit        _sk_sevenbit

void f (char *x);
int keycrunch (char *result,const char *seed,const char *passwd);
char *btoe (char *engout,char *c);
char *put8 (char *out,char *s);
int atob8 (char *out, char *in);
int btoa8 (char *out, char *in);
int htoi (char c);
int etob (char *out,char *e);
void sevenbit (char *s);
char *readpass (char *buf, int n);
void rip (char *buf);
#endif  /* _SKEY_INTERNAL */

/* Simplified application programming interface. */
#include <pwd.h>
int skeylookup (struct skey *mp, const char *name);
int skeyverify (struct skey *mp, char *response);
int skeychallenge (struct skey *mp, const char *name, char *challenge);
int skeyinfo (struct skey *mp, const char* name, char *ss);
int skeyaccess (char *user, const char *port, const char *host, const char *addr);
char *skey_getpass (const char *prompt, struct passwd * pwd, int pwok);
const char *skey_crypt (char *pp, char *salt, struct passwd *pwd, int pwok);

#endif /* _SKEY_H_ */
