/* $FreeBSD: src/usr.sbin/pkg_install/sign/pgp.h,v 1.1.2.1 2001/03/05 03:43:53 wes Exp $ */
/* $DragonFly: src/usr.sbin/pkg_install/sign/Attic/pgp.h,v 1.3 2003/11/03 19:31:39 eirikn Exp $ */
/* $OpenBSD: pgp.h,v 1.2 1999/10/04 21:46:28 espie Exp $ */
/* Estimate size of pgp signature */
#define MAXPGPSIGNSIZE	1024

#ifndef PGP
#define PGP "/usr/local/bin/pgp"
#endif

struct mygzip_header;
struct signature;

extern void *new_pgp_checker(struct mygzip_header *h, \
	struct signature *sign, const char *userid, char *envp[], \
	const char *filename);

extern void pgp_add(void *arg, const char *buffer, \
	size_t length);

extern int pgp_sign_ok(void *arg);

extern void handle_pgp_passphrase(void);

extern int retrieve_pgp_signature(const char *filename, \
struct signature **sign, const char *userid, char *envp[]);
