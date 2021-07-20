/*
 * Copyright (c) 2000-2003,2005,2012,2016,2019,2021 by Solar Designer
 * Copyright (c) 2017,2018 by Dmitry V. Levin
 * Copyright (c) 2017,2018 by Oleg Solovyov
 * See LICENSE
 */

#if defined(__FreeBSD__) || defined(__DragonFly__)
/* For vsnprintf(3) */
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#define _XOPEN_SOURCE_EXTENDED
#define _XOPEN_VERSION 500
#define _DEFAULT_SOURCE
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
#ifdef HAVE_LIBAUDIT
#include <security/pam_modutil.h>
#include <libaudit.h>
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

#include "passwdqc_i18n.h"

#define PROMPT_OLDPASS \
	_("Enter current password: ")
#define PROMPT_NEWPASS1 \
	_("Enter new password: ")
#define PROMPT_NEWPASS2 \
	_("Re-type new password: ")

#define MESSAGE_MISCONFIGURED \
	_("System configuration error.  Please contact your administrator.")
#define MESSAGE_INVALID_OPTION \
	"pam_passwdqc: %s."
#define MESSAGE_INTRO_PASSWORD \
	_("\nYou can now choose the new password.\n")
#define MESSAGE_INTRO_BOTH \
	_("\nYou can now choose the new password or passphrase.\n")

#define MESSAGE_EXPLAIN_PASSWORD_1_CLASS(count) \
	P3_( \
	"A good password should be a mix of upper and lower case letters, digits, and\n" \
	"other characters.  You can use a password containing at least %d character.\n", \
	\
	"A good password should be a mix of upper and lower case letters, digits, and\n" \
	"other characters.  You can use a password containing at least %d characters.\n", \
	count), (count)

#define MESSAGE_EXPLAIN_PASSWORD_N_CLASSES(count) \
	P3_( \
	"A valid password should be a mix of upper and lower case letters, digits, and\n" \
	"other characters.  You can use a password containing at least %d character\n" \
	"from at least %d of these 4 classes.\n" \
	"An upper case letter that begins the password and a digit that ends it do not\n" \
	"count towards the number of character classes used.\n", \
	\
	"A valid password should be a mix of upper and lower case letters, digits, and\n" \
	"other characters.  You can use a password containing at least %d characters\n" \
	"from at least %d of these 4 classes.\n" \
	"An upper case letter that begins the password and a digit that ends it do not\n" \
	"count towards the number of character classes used.\n", \
	count), (count)

#define MESSAGE_EXPLAIN_PASSWORD_ALL_CLASSES(count) \
	P3_( \
	"A valid password should be a mix of upper and lower case letters, digits, and\n" \
	"other characters.  You can use a password containing at least %d character\n" \
	"from all of these classes.\n" \
	"An upper case letter that begins the password and a digit that ends it do not\n" \
	"count towards the number of character classes used.\n", \
	\
	"A valid password should be a mix of upper and lower case letters, digits, and\n" \
	"other characters.  You can use a password containing at least %d characters\n" \
	"from all of these classes.\n" \
	"An upper case letter that begins the password and a digit that ends it do not\n" \
	"count towards the number of character classes used.\n", \
	count), (count)

#define MESSAGE_EXPLAIN_PASSWORD_ALL_OR_3_CLASSES(count) \
	P3_( \
	"A valid password should be a mix of upper and lower case letters, digits, and\n" \
	"other characters.  You can use a password containing at least %d character\n" \
	"from all of these classes, or a password containing at least %d characters\n" \
	"from just 3 of these 4 classes.\n" \
	"An upper case letter that begins the password and a digit that ends it do not\n" \
	"count towards the number of character classes used.\n", \
	\
	"A valid password should be a mix of upper and lower case letters, digits, and\n" \
	"other characters.  You can use a password containing at least %d characters\n" \
	"from all of these classes, or a password containing at least %d characters\n" \
	"from just 3 of these 4 classes.\n" \
	"An upper case letter that begins the password and a digit that ends it do not\n" \
	"count towards the number of character classes used.\n", \
	count), (count)

#define MESSAGE_EXPLAIN_PASSPHRASE(count) \
	P3_(\
	"A passphrase should be of at least %d word, %d to %d characters long, and\n" \
	"contain enough different characters.\n", \
	\
	"A passphrase should be of at least %d words, %d to %d characters long, and\n" \
	"contain enough different characters.\n", \
	count), (count)

#define MESSAGE_RANDOM \
	_("Alternatively, if no one else can see your terminal now, you can pick this as\n" \
	"your password: \"%s\".\n")
#define MESSAGE_RANDOMONLY \
	_("This system is configured to permit randomly generated passwords only.\n" \
	"If no one else can see your terminal now, you can pick this as your\n" \
	"password: \"%s\".  Otherwise come back later.\n")
#define MESSAGE_RANDOMFAILED \
	_("This system is configured to use randomly generated passwords only,\n" \
	"but the attempt to generate a password has failed.  This could happen\n" \
	"for a number of reasons: you could have requested an impossible password\n" \
	"length, or the access to kernel random number pool could have failed.")
#define MESSAGE_TOOLONG \
	_("This password may be too long for some services.  Choose another.")
#define MESSAGE_TRUNCATED \
	_("Warning: your longer password will be truncated to 8 characters.")
#define MESSAGE_WEAKPASS \
	_("Weak password: %s.")
#define MESSAGE_NOTRANDOM \
	_("Sorry, you've mistyped the password that was generated for you.")
#define MESSAGE_MISTYPED \
	_("Sorry, passwords do not match.")
#define MESSAGE_RETRY \
	_("Try again.")

static int logaudit(pam_handle_t *pamh, int status, passwdqc_params_t *params)
{
#ifdef HAVE_LIBAUDIT
	if (!(params->pam.flags & F_NO_AUDIT)) {
		int rc = pam_modutil_audit_write(pamh, AUDIT_USER_CHAUTHTOK, "pam_passwdqc", status);
		if (status == PAM_SUCCESS)
		       status = rc;
	}
#else /* !HAVE_LIBAUDIT */
	(void) pamh;
#endif
	passwdqc_params_free(params);
	return status;
}

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
		pwqc_drop_pam_reply(resp, 1);
	} else {
		status = PAM_ABORT;
	}
	_passwdqc_memzero(buffer, sizeof(buffer));

	return status;
}

static int check_max(passwdqc_params_qc_t *qc, pam_handle_t *pamh,
    const char *newpass)
{
	if (strlen(newpass) > (size_t)qc->max) {
		if (qc->max != 8) {
			say(pamh, PAM_ERROR_MSG, MESSAGE_TOOLONG);
			return -1;
		}
		say(pamh, PAM_TEXT_INFO, MESSAGE_TRUNCATED);
	}

	return 0;
}

static int check_pass(struct passwd *pw, const char *pass)
{
	const char *hash;
	int retval;

#ifdef HAVE_SHADOW
#ifdef __hpux
	if (iscomsec()) {
#else
	if (!strcmp(pw->pw_passwd, "x")) {
#endif
		struct spwd *spw = getspnam(pw->pw_name);
		endspent();
		if (!spw)
			return -1;
		hash = NULL;
		if (strlen(spw->sp_pwdp) >= 13) {
#ifdef __hpux
			hash = bigcrypt(pass, spw->sp_pwdp);
#else
			hash = crypt(pass, spw->sp_pwdp);
#endif
		}
		retval = (hash && !strcmp(hash, spw->sp_pwdp)) ? 0 : -1;
		_passwdqc_memzero(spw->sp_pwdp, strlen(spw->sp_pwdp));
		return retval;
	}
#endif

	hash = NULL;
	if (strlen(pw->pw_passwd) >= 13)
		hash = crypt(pass, pw->pw_passwd);
	retval = (hash && !strcmp(hash, pw->pw_passwd)) ? 0 : -1;
	_passwdqc_memzero(pw->pw_passwd, strlen(pw->pw_passwd));
	return retval;
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

PAM_EXTERN int pam_sm_chauthtok(pam_handle_t *pamh, int flags,
    int argc, const char **argv)
{
	passwdqc_params_t params;
	struct pam_response *resp;
	struct passwd *pw, fake_pw;
	pam_item_t item;
	const char *user, *oldpass, *newpass;
	char *trypass, *randompass;
	char *parse_reason;
	const char *check_reason;
	int ask_oldauthtok;
	int randomonly, enforce, retries_left, retry_wanted;
	int status;

	passwdqc_params_reset(&params);
	if (passwdqc_params_parse(&params, &parse_reason, argc, argv)) {
		say(pamh, PAM_ERROR_MSG, am_root(pamh) ?
		    MESSAGE_INVALID_OPTION : MESSAGE_MISCONFIGURED,
		    parse_reason);
		free(parse_reason);
		return PAM_ABORT;
	}
	status = PAM_SUCCESS;

	ask_oldauthtok = 0;
	if (flags & PAM_PRELIM_CHECK) {
		if (params.pam.flags & F_ASK_OLDAUTHTOK_PRELIM)
			ask_oldauthtok = 1;
	} else if (flags & PAM_UPDATE_AUTHTOK) {
		if (params.pam.flags & F_ASK_OLDAUTHTOK_UPDATE)
			ask_oldauthtok = 1;
	} else {
		passwdqc_params_free(&params);
		return PAM_SERVICE_ERR;
	}

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
			return logaudit(pamh, status, &params);
	}

	if (flags & PAM_PRELIM_CHECK) {
		passwdqc_params_free(&params);
		return status;
	}

	status = pam_get_item(pamh, PAM_USER, &item);
	if (status != PAM_SUCCESS)
		return logaudit(pamh, status, &params);
	user = item;

	status = pam_get_item(pamh, PAM_OLDAUTHTOK, &item);
	if (status != PAM_SUCCESS)
		return logaudit(pamh, status, &params);
	oldpass = item;

	if (params.pam.flags & F_NON_UNIX) {
		pw = &fake_pw;
		memset(pw, 0, sizeof(*pw));
		pw->pw_name = (char *)user;
		pw->pw_gecos = "";
		pw->pw_dir = "";
	} else {
/* As currently implemented, we don't avoid timing leaks for valid vs. not
 * usernames and hashes.  Normally, the username would have already been
 * checked and determined valid, and the check_oldauthtok option is only needed
 * on systems that happen to have similar timing leaks all over the place. */
		pw = getpwnam(user);
		endpwent();
		if (!pw)
			return logaudit(pamh, PAM_USER_UNKNOWN, &params);
		if ((params.pam.flags & F_CHECK_OLDAUTHTOK) && !am_root(pamh)
		    && (!oldpass || check_pass(pw, oldpass)))
			status = PAM_AUTH_ERR;
		_passwdqc_memzero(pw->pw_passwd, strlen(pw->pw_passwd));
		if (status != PAM_SUCCESS)
			return logaudit(pamh, status, &params);
	}

	randomonly = params.qc.min[4] > params.qc.max;

	if (am_root(pamh))
		enforce = params.pam.flags & F_ENFORCE_ROOT;
	else
		enforce = params.pam.flags & F_ENFORCE_USERS;

	if (params.pam.flags & F_USE_AUTHTOK) {
		status = pam_get_item(pamh, PAM_AUTHTOK, &item);
		if (status != PAM_SUCCESS)
			return logaudit(pamh, status, &params);
		newpass = item;
		if (!newpass ||
		    (check_max(&params.qc, pamh, newpass) && enforce))
			return logaudit(pamh, PAM_AUTHTOK_ERR, &params);
		check_reason =
		    passwdqc_check(&params.qc, newpass, oldpass, pw);
		if (check_reason) {
			say(pamh, PAM_ERROR_MSG, MESSAGE_WEAKPASS,
			    check_reason);
			if (enforce)
				status = PAM_AUTHTOK_ERR;
		}
		return logaudit(pamh, status, &params);
	}

	retries_left = params.pam.retry;

retry:
	retry_wanted = 0;

	if (!randomonly &&
	    params.qc.passphrase_words && params.qc.min[2] <= params.qc.max)
		status = say(pamh, PAM_TEXT_INFO, MESSAGE_INTRO_BOTH);
	else
		status = say(pamh, PAM_TEXT_INFO, MESSAGE_INTRO_PASSWORD);
	if (status != PAM_SUCCESS)
		return logaudit(pamh, status, &params);

	if (!randomonly && params.qc.min[0] == params.qc.min[4])
		status = say(pamh, PAM_TEXT_INFO,
		    MESSAGE_EXPLAIN_PASSWORD_1_CLASS(params.qc.min[4]));

	else if (!randomonly && params.qc.min[3] == params.qc.min[4])
		status = say(pamh, PAM_TEXT_INFO,
		    MESSAGE_EXPLAIN_PASSWORD_N_CLASSES(params.qc.min[4]),
		    params.qc.min[1] != params.qc.min[3] ? 3 : 2);
	else if (!randomonly && params.qc.min[3] == INT_MAX)
		status = say(pamh, PAM_TEXT_INFO,
		    MESSAGE_EXPLAIN_PASSWORD_ALL_CLASSES(params.qc.min[4]));
	else if (!randomonly) {
		status = say(pamh, PAM_TEXT_INFO,
		    MESSAGE_EXPLAIN_PASSWORD_ALL_OR_3_CLASSES(params.qc.min[4]),
		    params.qc.min[3]);
	}
	if (status != PAM_SUCCESS)
		return logaudit(pamh, status, &params);

	if (!randomonly &&
	    params.qc.passphrase_words && params.qc.min[2] <= params.qc.max) {
		status = say(pamh, PAM_TEXT_INFO,
		    MESSAGE_EXPLAIN_PASSPHRASE(params.qc.passphrase_words),
		    params.qc.min[2], params.qc.max);
		if (status != PAM_SUCCESS)
			return logaudit(pamh, status, &params);
	}

	randompass = passwdqc_random(&params.qc);
	if (randompass) {
		status = say(pamh, PAM_TEXT_INFO, randomonly ?
		    MESSAGE_RANDOMONLY : MESSAGE_RANDOM, randompass);
		if (status != PAM_SUCCESS) {
			pwqc_overwrite_string(randompass);
			pwqc_drop_mem(randompass);
		}
	} else if (randomonly) {
		say(pamh, PAM_ERROR_MSG, am_root(pamh) ?
		    MESSAGE_RANDOMFAILED : MESSAGE_MISCONFIGURED);
		return logaudit(pamh, PAM_AUTHTOK_ERR, &params);
	}

	status = converse(pamh, PAM_PROMPT_ECHO_OFF, PROMPT_NEWPASS1, &resp);
	if (status == PAM_SUCCESS && (!resp || !resp->resp))
		status = PAM_AUTHTOK_ERR;

	if (status != PAM_SUCCESS) {
		pwqc_overwrite_string(randompass);
		pwqc_drop_mem(randompass);
		return logaudit(pamh, status, &params);
	}

	trypass = strdup(resp->resp);

	pwqc_drop_pam_reply(resp, 1);

	if (!trypass) {
		pwqc_overwrite_string(randompass);
		pwqc_drop_mem(randompass);
		return logaudit(pamh, PAM_AUTHTOK_ERR, &params);
	}

	if (check_max(&params.qc, pamh, trypass) && enforce) {
		status = PAM_AUTHTOK_ERR;
		retry_wanted = 1;
	}

	check_reason = NULL; /* unused */
	if (status == PAM_SUCCESS &&
	    (!randompass || !strstr(trypass, randompass)) &&
	    (randomonly ||
	     (check_reason = passwdqc_check(&params.qc, trypass, oldpass, pw)))) {
		if (randomonly)
			say(pamh, PAM_ERROR_MSG, MESSAGE_NOTRANDOM);
		else
			say(pamh, PAM_ERROR_MSG, MESSAGE_WEAKPASS,
			    check_reason);
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
	pwqc_drop_mem(randompass);

	pwqc_overwrite_string(trypass);
	pwqc_drop_mem(trypass);

	if (retry_wanted && --retries_left > 0) {
		status = say(pamh, PAM_TEXT_INFO, MESSAGE_RETRY);
		if (status == PAM_SUCCESS)
			goto retry;
	}

	return logaudit(pamh, status, &params);
}

#ifdef PAM_MODULE_ENTRY
PAM_MODULE_ENTRY("pam_passwdqc");
#elif defined(PAM_STATIC)
const struct pam_module _pam_passwdqc_modstruct = {
	"pam_passwdqc",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	pam_sm_chauthtok
};
#endif
