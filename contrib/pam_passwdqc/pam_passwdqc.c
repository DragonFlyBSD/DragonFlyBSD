/*
 * Copyright (c) 2000-2003,2005 by Solar Designer.  See LICENSE.
 */

#if defined(__FreeBSD__) || defined(__DragonFly__)
/* For vsnprintf(3) */
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#define _XOPEN_SOURCE_EXTENDED
#define _XOPEN_VERSION 500
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <pwd.h>
#ifdef HAVE_SHADOW
#include <shadow.h>
#endif

#define PAM_SM_PASSWORD
#ifndef LINUX_PAM
#include <security/pam_appl.h>
#endif
#include <security/pam_modules.h>

#include "pam_macros.h"

#if !defined(PAM_EXTERN) && !defined(PAM_STATIC)
#define PAM_EXTERN			extern
#endif

#if !defined(PAM_AUTHTOK_RECOVERY_ERR) && defined(PAM_AUTHTOK_RECOVER_ERR)
#define PAM_AUTHTOK_RECOVERY_ERR	PAM_AUTHTOK_RECOVER_ERR
#endif

#if (defined(__sun) || defined(__hpux)) && \
    !defined(LINUX_PAM) && !defined(_OPENPAM)
/* Sun's PAM doesn't use const here, while Linux-PAM and OpenPAM do */
#define lo_const
#else
#define lo_const			const
#endif
#ifdef _OPENPAM
/* OpenPAM doesn't use const here, while Linux-PAM does */
#define l_const
#else
#define l_const				lo_const
#endif
typedef lo_const void *pam_item_t;

#include "passwdqc.h"

#define F_ENFORCE_MASK			0x00000003
#define F_ENFORCE_USERS			0x00000001
#define F_ENFORCE_ROOT			0x00000002
#define F_ENFORCE_EVERYONE		F_ENFORCE_MASK
#define F_NON_UNIX			0x00000004
#define F_ASK_OLDAUTHTOK_MASK		0x00000030
#define F_ASK_OLDAUTHTOK_PRELIM		0x00000010
#define F_ASK_OLDAUTHTOK_UPDATE		0x00000020
#define F_CHECK_OLDAUTHTOK		0x00000040
#define F_USE_FIRST_PASS		0x00000100
#define F_USE_AUTHTOK			0x00000200

typedef struct {
	passwdqc_params_t qc;
	int flags;
	int retry;
} params_t;

static params_t defaults = {
	{
		{INT_MAX, 24, 11, 8, 7},	/* min */
		40,				/* max */
		3,				/* passphrase_words */
		4,				/* match_length */
		1,				/* similar_deny */
		42				/* random_bits */
	},
	F_ENFORCE_EVERYONE,			/* flags */
	3					/* retry */
};

#define PROMPT_OLDPASS \
	"Enter current password: "
#define PROMPT_NEWPASS1 \
	"Enter new password: "
#define PROMPT_NEWPASS2 \
	"Re-type new password: "

#define MESSAGE_MISCONFIGURED \
	"System configuration error.  Please contact your administrator."
#define MESSAGE_INVALID_OPTION \
	"pam_passwdqc: Invalid option: \"%s\"."
#define MESSAGE_INTRO_PASSWORD \
	"\nYou can now choose the new password.\n"
#define MESSAGE_INTRO_BOTH \
	"\nYou can now choose the new password or passphrase.\n"
#define MESSAGE_EXPLAIN_PASSWORD_1CLASS \
	"A good password should be a mix of upper and lower case letters,\n" \
	"digits, and other characters.  You can use a%s %d character long\n" \
	"password.\n"
#define MESSAGE_EXPLAIN_PASSWORD_CLASSES \
	"A valid password should be a mix of upper and lower case letters,\n" \
	"digits, and other characters.  You can use a%s %d character long\n" \
	"password with characters from at least %d of these 4 classes.\n" \
	"An upper case letter that begins the password and a digit that\n" \
	"ends it do not count towards the number of character classes used.\n"
#define MESSAGE_EXPLAIN_PASSWORD_ALL_CLASSES \
	"A valid password should be a mix of upper and lower case letters,\n" \
	"digits, and other characters.  You can use a%s %d character long\n" \
	"password with characters from all of these classes.  An upper\n" \
	"case letter that begins the password and a digit that ends it do\n" \
	"not count towards the number of character classes used.\n"
#define MESSAGE_EXPLAIN_PASSWORD_ALT \
	"A valid password should be a mix of upper and lower case letters,\n" \
	"digits, and other characters.  You can use a%s %d character long\n" \
	"password with characters from at least 3 of these 4 classes, or\n" \
	"a%s %d character long password containing characters from all the\n" \
	"classes.  An upper case letter that begins the password and a\n" \
	"digit that ends it do not count towards the number of character\n" \
	"classes used.\n"
#define MESSAGE_EXPLAIN_PASSPHRASE \
	"A passphrase should be of at least %d words, %d to %d characters\n" \
	"long, and contain enough different characters.\n"
#define MESSAGE_RANDOM \
	"Alternatively, if noone else can see your terminal now, you can\n" \
	"pick this as your password: \"%s\".\n"
#define MESSAGE_RANDOMONLY \
	"This system is configured to permit randomly generated passwords\n" \
	"only.  If noone else can see your terminal now, you can pick this\n" \
	"as your password: \"%s\".  Otherwise, come back later.\n"
#define MESSAGE_RANDOMFAILED \
	"This system is configured to use randomly generated passwords\n" \
	"only, but the attempt to generate a password has failed.  This\n" \
	"could happen for a number of reasons: you could have requested\n" \
	"an impossible password length, or the access to kernel random\n" \
	"number pool could have failed."
#define MESSAGE_TOOLONG \
	"This password may be too long for some services.  Choose another."
#define MESSAGE_TRUNCATED \
	"Warning: your longer password will be truncated to 8 characters."
#define MESSAGE_WEAKPASS \
	"Weak password: %s."
#define MESSAGE_NOTRANDOM \
	"Sorry, you've mistyped the password that was generated for you."
#define MESSAGE_MISTYPED \
	"Sorry, passwords do not match."
#define MESSAGE_RETRY \
	"Try again."

static int converse(pam_handle_t *pamh, int style, l_const char *text,
    struct pam_response **resp)
{
	pam_item_t item;
	const struct pam_conv *conv;
	struct pam_message msg, *pmsg;
	int status;

	*resp = NULL;
	status = pam_get_item(pamh, PAM_CONV, &item);
	if (status != PAM_SUCCESS)
		return status;
	conv = item;

	pmsg = &msg;
	msg.msg_style = style;
	msg.msg = (char *)text;

	return conv->conv(1, (lo_const struct pam_message **)&pmsg, resp,
	    conv->appdata_ptr);
}

#ifdef __GNUC__
__attribute__ ((format (printf, 3, 4)))
#endif
static int say(pam_handle_t *pamh, int style, const char *format, ...)
{
	va_list args;
	char buffer[0x800];
	int needed;
	struct pam_response *resp;
	int status;

	va_start(args, format);
	needed = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	if ((unsigned int)needed < sizeof(buffer)) {
		status = converse(pamh, style, buffer, &resp);
		pwqc_overwrite_string(buffer);
		pwqc_drop_pam_reply(resp, 1);
	} else {
		status = PAM_ABORT;
		memset(buffer, 0, sizeof(buffer));
	}

	return status;
}

static int check_max(params_t *params, pam_handle_t *pamh, const char *newpass)
{
	if ((int)strlen(newpass) > params->qc.max) {
		if (params->qc.max != 8) {
			say(pamh, PAM_ERROR_MSG, MESSAGE_TOOLONG);
			return -1;
		}
		say(pamh, PAM_TEXT_INFO, MESSAGE_TRUNCATED);
	}

	return 0;
}

static int check_pass(struct passwd *pw, const char *pass)
{
#ifdef HAVE_SHADOW
	struct spwd *spw;
	const char *hash;
	int retval;

#ifdef __hpux
	if (iscomsec()) {
#else
	if (!strcmp(pw->pw_passwd, "x")) {
#endif
		spw = getspnam(pw->pw_name);
		endspent();
		if (!spw)
			return -1;
#ifdef __hpux
		hash = bigcrypt(pass, spw->sp_pwdp);
#else
		hash = crypt(pass, spw->sp_pwdp);
#endif
		retval = strcmp(hash, spw->sp_pwdp) ? -1 : 0;
		memset(spw->sp_pwdp, 0, strlen(spw->sp_pwdp));
		return retval;
	}
#endif

	return strcmp(crypt(pass, pw->pw_passwd), pw->pw_passwd) ? -1 : 0;
}

static int am_root(pam_handle_t *pamh)
{
	pam_item_t item;
	const char *service;

	if (getuid() != 0)
		return 0;

	if (pam_get_item(pamh, PAM_SERVICE, &item) != PAM_SUCCESS)
		return 0;
	service = item;

	return !strcmp(service, "passwd");
}

static int parse(params_t *params, pam_handle_t *pamh,
    int argc, const char **argv)
{
	const char *p;
	char *e;
	int i;
	unsigned long v;

	while (argc) {
		if (!strncmp(*argv, "min=", 4)) {
			p = *argv + 4;
			for (i = 0; i < 5; i++) {
				if (!strncmp(p, "disabled", 8)) {
					v = INT_MAX;
					p += 8;
				} else {
					v = strtoul(p, &e, 10);
					p = e;
				}
				if (i < 4 && *p++ != ',') break;
				if (v > INT_MAX) break;
				if (i && (int)v > params->qc.min[i - 1]) break;
				params->qc.min[i] = v;
			}
			if (*p) break;
		} else
		if (!strncmp(*argv, "max=", 4)) {
			v = strtoul(*argv + 4, &e, 10);
			if (*e || v < 8 || v > INT_MAX) break;
			params->qc.max = v;
		} else
		if (!strncmp(*argv, "passphrase=", 11)) {
			v = strtoul(*argv + 11, &e, 10);
			if (*e || v > INT_MAX) break;
			params->qc.passphrase_words = v;
		} else
		if (!strncmp(*argv, "match=", 6)) {
			v = strtoul(*argv + 6, &e, 10);
			if (*e || v > INT_MAX) break;
			params->qc.match_length = v;
		} else
		if (!strncmp(*argv, "similar=", 8)) {
			if (!strcmp(*argv + 8, "permit"))
				params->qc.similar_deny = 0;
			else
			if (!strcmp(*argv + 8, "deny"))
				params->qc.similar_deny = 1;
			else
				break;
		} else
		if (!strncmp(*argv, "random=", 7)) {
			v = strtoul(*argv + 7, &e, 10);
			if (!strcmp(e, ",only")) {
				e += 5;
				params->qc.min[4] = INT_MAX;
			}
			if (*e || (v && v < 24) || v > 72) break;
			params->qc.random_bits = v;
		} else
		if (!strncmp(*argv, "enforce=", 8)) {
			params->flags &= ~F_ENFORCE_MASK;
			if (!strcmp(*argv + 8, "users"))
				params->flags |= F_ENFORCE_USERS;
			else
			if (!strcmp(*argv + 8, "everyone"))
				params->flags |= F_ENFORCE_EVERYONE;
			else
			if (strcmp(*argv + 8, "none"))
				break;
		} else
		if (!strcmp(*argv, "non-unix")) {
			if (params->flags & F_CHECK_OLDAUTHTOK) break;
			params->flags |= F_NON_UNIX;
		} else
		if (!strncmp(*argv, "retry=", 6)) {
			v = strtoul(*argv + 6, &e, 10);
			if (*e || v > INT_MAX) break;
			params->retry = v;
		} else
		if (!strncmp(*argv, "ask_oldauthtok", 14)) {
			params->flags &= ~F_ASK_OLDAUTHTOK_MASK;
			if (params->flags & F_USE_FIRST_PASS) break;
			if (!strcmp(*argv + 14, "=update"))
				params->flags |= F_ASK_OLDAUTHTOK_UPDATE;
			else
			if (!(*argv)[14])
				params->flags |= F_ASK_OLDAUTHTOK_PRELIM;
			else
				break;
		} else
		if (!strcmp(*argv, "check_oldauthtok")) {
			if (params->flags & F_NON_UNIX) break;
			params->flags |= F_CHECK_OLDAUTHTOK;
		} else
		if (!strcmp(*argv, "use_first_pass")) {
			if (params->flags & F_ASK_OLDAUTHTOK_MASK) break;
			params->flags |= F_USE_FIRST_PASS | F_USE_AUTHTOK;
		} else
		if (!strcmp(*argv, "use_authtok")) {
			params->flags |= F_USE_AUTHTOK;
		} else
			break;
		argc--; argv++;
	}

	if (argc) {
		say(pamh, PAM_ERROR_MSG, am_root(pamh) ?
		    MESSAGE_INVALID_OPTION : MESSAGE_MISCONFIGURED, *argv);
		return PAM_ABORT;
	}

	return PAM_SUCCESS;
}

PAM_EXTERN int pam_sm_chauthtok(pam_handle_t *pamh, int flags,
    int argc, const char **argv)
{
	params_t params;
	struct pam_response *resp;
	struct passwd *pw, fake_pw;
	pam_item_t item;
	const char *user, *oldpass, *newpass;
	char *trypass, *randompass;
	const char *reason;
	int ask_oldauthtok;
	int randomonly, enforce, retries_left, retry_wanted;
	int status;

	params = defaults;
	status = parse(&params, pamh, argc, argv);
	if (status != PAM_SUCCESS)
		return status;

	ask_oldauthtok = 0;
	if (flags & PAM_PRELIM_CHECK) {
		if (params.flags & F_ASK_OLDAUTHTOK_PRELIM)
			ask_oldauthtok = 1;
	} else
	if (flags & PAM_UPDATE_AUTHTOK) {
		if (params.flags & F_ASK_OLDAUTHTOK_UPDATE)
			ask_oldauthtok = 1;
	} else
		return PAM_SERVICE_ERR;

	if (ask_oldauthtok && !am_root(pamh)) {
		status = converse(pamh, PAM_PROMPT_ECHO_OFF,
		    PROMPT_OLDPASS, &resp);

		if (status == PAM_SUCCESS) {
			if (resp && resp->resp) {
				status = pam_set_item(pamh,
				    PAM_OLDAUTHTOK, resp->resp);
				pwqc_drop_pam_reply(resp, 1);
			} else
				status = PAM_AUTHTOK_RECOVERY_ERR;
		}

		if (status != PAM_SUCCESS)
			return status;
	}

	if (flags & PAM_PRELIM_CHECK)
		return status;

	status = pam_get_item(pamh, PAM_USER, &item);
	if (status != PAM_SUCCESS)
		return status;
	user = item;

	status = pam_get_item(pamh, PAM_OLDAUTHTOK, &item);
	if (status != PAM_SUCCESS)
		return status;
	oldpass = item;

	if (params.flags & F_NON_UNIX) {
		pw = &fake_pw;
		pw->pw_name = (char *)user;
		pw->pw_gecos = "";
	} else {
		pw = getpwnam(user);
		endpwent();
		if (!pw)
			return PAM_USER_UNKNOWN;
		if ((params.flags & F_CHECK_OLDAUTHTOK) && !am_root(pamh) &&
		    (!oldpass || check_pass(pw, oldpass)))
			status = PAM_AUTH_ERR;
		memset(pw->pw_passwd, 0, strlen(pw->pw_passwd));
		if (status != PAM_SUCCESS)
			return status;
	}

	randomonly = params.qc.min[4] > params.qc.max;

	if (am_root(pamh))
		enforce = params.flags & F_ENFORCE_ROOT;
	else
		enforce = params.flags & F_ENFORCE_USERS;

	if (params.flags & F_USE_AUTHTOK) {
		status = pam_get_item(pamh, PAM_AUTHTOK, &item);
		if (status != PAM_SUCCESS)
			return status;
		newpass = item;
		if (!newpass || (check_max(&params, pamh, newpass) && enforce))
			return PAM_AUTHTOK_ERR;
		reason = _passwdqc_check(&params.qc, newpass, oldpass, pw);
		if (reason) {
			say(pamh, PAM_ERROR_MSG, MESSAGE_WEAKPASS, reason);
			if (enforce)
				status = PAM_AUTHTOK_ERR;
		}
		return status;
	}

	retries_left = params.retry;

retry:
	retry_wanted = 0;

	if (!randomonly &&
	    params.qc.passphrase_words && params.qc.min[2] <= params.qc.max)
		status = say(pamh, PAM_TEXT_INFO, MESSAGE_INTRO_BOTH);
	else
		status = say(pamh, PAM_TEXT_INFO, MESSAGE_INTRO_PASSWORD);
	if (status != PAM_SUCCESS)
		return status;

	if (!randomonly && params.qc.min[0] == params.qc.min[4])
		status = say(pamh, PAM_TEXT_INFO,
		    MESSAGE_EXPLAIN_PASSWORD_1CLASS,
		    params.qc.min[4] == 8 || params.qc.min[4] == 11 ? "n" : "",
		    params.qc.min[4]);
	else
	if (!randomonly && params.qc.min[3] == params.qc.min[4])
		status = say(pamh, PAM_TEXT_INFO,
		    MESSAGE_EXPLAIN_PASSWORD_CLASSES,
		    params.qc.min[4] == 8 || params.qc.min[4] == 11 ? "n" : "",
		    params.qc.min[4],
		    params.qc.min[1] != params.qc.min[3] ? 3 : 2);
	else
	if (!randomonly && params.qc.min[3] == INT_MAX)
		status = say(pamh, PAM_TEXT_INFO,
		    MESSAGE_EXPLAIN_PASSWORD_ALL_CLASSES,
		    params.qc.min[4] == 8 || params.qc.min[4] == 11 ? "n" : "",
		    params.qc.min[4]);
	else
	if (!randomonly)
		status = say(pamh, PAM_TEXT_INFO,
		    MESSAGE_EXPLAIN_PASSWORD_ALT,
		    params.qc.min[3] == 8 || params.qc.min[3] == 11 ? "n" : "",
		    params.qc.min[3],
		    params.qc.min[4] == 8 || params.qc.min[4] == 11 ? "n" : "",
		    params.qc.min[4]);
	if (status != PAM_SUCCESS)
		return status;

	if (!randomonly &&
	    params.qc.passphrase_words &&
	    params.qc.min[2] <= params.qc.max) {
		status = say(pamh, PAM_TEXT_INFO, MESSAGE_EXPLAIN_PASSPHRASE,
		    params.qc.passphrase_words,
		    params.qc.min[2], params.qc.max);
		if (status != PAM_SUCCESS)
			return status;
	}

	randompass = _passwdqc_random(&params.qc);
	if (randompass) {
		status = say(pamh, PAM_TEXT_INFO, randomonly ?
		    MESSAGE_RANDOMONLY : MESSAGE_RANDOM, randompass);
		if (status != PAM_SUCCESS) {
			pwqc_overwrite_string(randompass);
			randompass = NULL;
		}
	} else
	if (randomonly) {
		say(pamh, PAM_ERROR_MSG, am_root(pamh) ?
		    MESSAGE_RANDOMFAILED : MESSAGE_MISCONFIGURED);
		return PAM_AUTHTOK_ERR;
	}

	status = converse(pamh, PAM_PROMPT_ECHO_OFF, PROMPT_NEWPASS1, &resp);
	if (status == PAM_SUCCESS && (!resp || !resp->resp))
		status = PAM_AUTHTOK_ERR;

	if (status != PAM_SUCCESS) {
		pwqc_overwrite_string(randompass);
		return status;
	}

	trypass = strdup(resp->resp);

	pwqc_drop_pam_reply(resp, 1);

	if (!trypass) {
		pwqc_overwrite_string(randompass);
		return PAM_AUTHTOK_ERR;
	}

	if (check_max(&params, pamh, trypass) && enforce) {
		status = PAM_AUTHTOK_ERR;
		retry_wanted = 1;
	}

	reason = NULL;
	if (status == PAM_SUCCESS &&
	    (!randompass || !strstr(trypass, randompass)) &&
	    (randomonly ||
	    (reason = _passwdqc_check(&params.qc, trypass, oldpass, pw)))) {
		if (randomonly)
			say(pamh, PAM_ERROR_MSG, MESSAGE_NOTRANDOM);
		else
			say(pamh, PAM_ERROR_MSG, MESSAGE_WEAKPASS, reason);
		if (enforce) {
			status = PAM_AUTHTOK_ERR;
			retry_wanted = 1;
		}
	}

	if (status == PAM_SUCCESS)
		status = converse(pamh, PAM_PROMPT_ECHO_OFF,
		    PROMPT_NEWPASS2, &resp);
	if (status == PAM_SUCCESS) {
		if (resp && resp->resp) {
			if (strcmp(trypass, resp->resp)) {
				status = say(pamh,
				    PAM_ERROR_MSG, MESSAGE_MISTYPED);
				if (status == PAM_SUCCESS) {
					status = PAM_AUTHTOK_ERR;
					retry_wanted = 1;
				}
			}
			pwqc_drop_pam_reply(resp, 1);
		} else
			status = PAM_AUTHTOK_ERR;
	}

	if (status == PAM_SUCCESS)
		status = pam_set_item(pamh, PAM_AUTHTOK, trypass);

	pwqc_overwrite_string(randompass);
	pwqc_overwrite_string(trypass);
	pwqc_drop_mem(trypass);

	if (retry_wanted && --retries_left > 0) {
		status = say(pamh, PAM_TEXT_INFO, MESSAGE_RETRY);
		if (status == PAM_SUCCESS)
			goto retry;
	}

	return status;
}

#ifdef PAM_MODULE_ENTRY
PAM_MODULE_ENTRY("pam_passwdqc");
#elif defined(PAM_STATIC)
struct pam_module _pam_passwdqc_modstruct = {
	"pam_passwdqc",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	pam_sm_chauthtok
};
#endif
