/*-
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/crypto/openssh/auth2-pam-freebsd.c,v 1.1.2.6 2003/04/07 09:56:46 des Exp $
 * $DragonFly: src/secure/usr.sbin/sshd/Attic/auth2-pam-freebsd.c,v 1.2 2004/08/30 21:59:58 geekgod Exp $
 */

#include "includes.h"

#ifdef USE_PAM
#include <security/pam_appl.h>

#include "auth.h"
#include "auth-pam.h"
#include "buffer.h"
#include "bufaux.h"
#include "canohost.h"
#include "log.h"
#include "monitor_wrap.h"
#include "msg.h"
#include "packet.h"
#include "misc.h"
#include "servconf.h"
#include "ssh2.h"
#include "xmalloc.h"

#ifdef USE_POSIX_THREADS
#include <pthread.h>
#else
typedef pid_t pthread_t;
#endif

struct pam_ctxt {
	pthread_t	 pam_thread;
	int		 pam_psock;
	int		 pam_csock;
	int		 pam_done;
};

static void sshpam_free_ctx(void *);
static struct pam_ctxt *cleanup_ctxt;
static Authctxt *sshpam_authctxt = NULL;
static const char *sshpam_password = NULL;


#ifndef USE_POSIX_THREADS
/*
 * Simulate threads with processes.
 */

static int sshpam_thread_status = -1;
static mysig_t sshpam_oldsig;

static void 
sshpam_sigchld_handler(int sig)
{
	if (cleanup_ctxt == NULL)
		return;	/* handler called after PAM cleanup, shouldn't happen */
	if (waitpid(cleanup_ctxt->pam_thread, &sshpam_thread_status, 0) == -1)
		return;	/* couldn't wait for process */
	if (WIFSIGNALED(sshpam_thread_status) &&
	    WTERMSIG(sshpam_thread_status) == SIGTERM)
		return;	/* terminated by pthread_cancel */
	if (!WIFEXITED(sshpam_thread_status))
		fatal("PAM: authentication thread exited unexpectedly");
	if (WEXITSTATUS(sshpam_thread_status) != 0)
		fatal("PAM: authentication thread exited uncleanly");
}


static void
pthread_exit(void *value __unused)
{
	_exit(0);
}

static int
pthread_create(pthread_t *thread, const void *attr __unused,
    void *(*thread_start)(void *), void *arg)
{
	pid_t pid;

	switch ((pid = fork())) {
	case -1:
		error("fork(): %s", strerror(errno));
		return (-1);
	case 0:
		thread_start(arg);
		_exit(1);
	default:
		*thread = pid;
		sshpam_oldsig = signal(SIGCHLD, sshpam_sigchld_handler);
		return (0);
	}
}

static int
pthread_cancel(pthread_t thread)
{
	signal(SIGCHLD, sshpam_oldsig);
	return (kill(thread, SIGTERM));
}

static int
pthread_join(pthread_t thread, void **value __unused)
{
	int status;

	if (sshpam_thread_status != -1)
		return (sshpam_thread_status);
	signal(SIGCHLD, sshpam_oldsig);
	waitpid(thread, &status, 0);
	return (status);
}
#endif


extern ServerOptions options;
extern Buffer loginmsg;
extern int compat20;
extern u_int utmp_len;

static pam_handle_t *sshpam_handle;
static int sshpam_err;
static int sshpam_authenticated;
static int sshpam_new_authtok_reqd;
static int sshpam_session_open;
static int sshpam_cred_established;

/*
 * Conversation function for authentication thread.
 */
static int
sshpam_thread_conv(int n,
	 const struct pam_message **msg,
	 struct pam_response **resp,
	 void *data)
{
	Buffer buffer;
	struct pam_ctxt *ctxt;
	int i;

	ctxt = data;
	if (n <= 0 || n > PAM_MAX_NUM_MSG)
		return (PAM_CONV_ERR);
	*resp = xmalloc(n * sizeof **resp);
	buffer_init(&buffer);
	for (i = 0; i < n; ++i) {
		resp[i]->resp_retcode = 0;
		resp[i]->resp = NULL;
		switch (msg[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
			buffer_put_cstring(&buffer, msg[i]->msg);
			ssh_msg_send(ctxt->pam_csock, msg[i]->msg_style, &buffer);
			ssh_msg_recv(ctxt->pam_csock, &buffer);
			if (buffer_get_char(&buffer) != PAM_AUTHTOK)
				goto fail;
			resp[i]->resp = buffer_get_string(&buffer, NULL);
			break;
		case PAM_PROMPT_ECHO_ON:
			buffer_put_cstring(&buffer, msg[i]->msg);
			ssh_msg_send(ctxt->pam_csock, msg[i]->msg_style, &buffer);
			ssh_msg_recv(ctxt->pam_csock, &buffer);
			if (buffer_get_char(&buffer) != PAM_AUTHTOK)
				goto fail;
			resp[i]->resp = buffer_get_string(&buffer, NULL);
			break;
		case PAM_ERROR_MSG:
			buffer_put_cstring(&buffer, msg[i]->msg);
			ssh_msg_send(ctxt->pam_csock, msg[i]->msg_style, &buffer);
			break;
		case PAM_TEXT_INFO:
			buffer_put_cstring(&buffer, msg[i]->msg);
			ssh_msg_send(ctxt->pam_csock, msg[i]->msg_style, &buffer);
			break;
		default:
			goto fail;
		}
		buffer_clear(&buffer);
	}
	buffer_free(&buffer);
	return (PAM_SUCCESS);
 fail:
	while (i)
		xfree(resp[--i]);
	xfree(*resp);
	*resp = NULL;
	buffer_free(&buffer);
	return (PAM_CONV_ERR);
}

/*
 * Authentication thread.
 */
static void *
sshpam_thread(void *ctxtp)
{
	struct pam_ctxt *ctxt = ctxtp;
	Buffer buffer;
	struct pam_conv pam_conv = { sshpam_thread_conv, ctxt };

#ifndef USE_POSIX_THREADS
	{
		const char *pam_user;

		pam_get_item(sshpam_handle, PAM_USER, (const void **)&pam_user);
		setproctitle("%s [pam]", pam_user);
	}
#endif
	buffer_init(&buffer);
	sshpam_err = pam_set_item(sshpam_handle, PAM_CONV, (const void *)&pam_conv);
	if (sshpam_err != PAM_SUCCESS)
		goto auth_fail;
	sshpam_err = pam_authenticate(sshpam_handle, 0);
	if (sshpam_err != PAM_SUCCESS)
		goto auth_fail;
	sshpam_err = pam_acct_mgmt(sshpam_handle, 0);
	if (sshpam_err != PAM_SUCCESS && sshpam_err != PAM_NEW_AUTHTOK_REQD)
		goto auth_fail;
	buffer_put_cstring(&buffer, "OK");
	ssh_msg_send(ctxt->pam_csock, sshpam_err, &buffer);
	buffer_free(&buffer);
	pthread_exit(NULL);
 auth_fail:
	buffer_put_cstring(&buffer,
	    pam_strerror(sshpam_handle, sshpam_err));
	ssh_msg_send(ctxt->pam_csock, PAM_AUTH_ERR, &buffer);
	buffer_free(&buffer);
	pthread_exit(NULL);
}

void
sshpam_thread_cleanup(void)
{
	struct pam_ctxt *ctxt = cleanup_ctxt;

	if (ctxt != NULL && ctxt->pam_thread != 0) {
		pthread_cancel(ctxt->pam_thread);
		pthread_join(ctxt->pam_thread, NULL);
		close(ctxt->pam_psock);
		close(ctxt->pam_csock);
		memset(ctxt, 0, sizeof(*ctxt));
		cleanup_ctxt = NULL;
	}
}

static int
sshpam_null_conv(int n,
	 const struct pam_message **msg,
	 struct pam_response **resp,
	 void *data)
{

	return (PAM_CONV_ERR);
}

static struct pam_conv null_conv = { sshpam_null_conv, NULL };

void
sshpam_cleanup(void)
{
	debug("PAM: cleanup");
	pam_set_item(sshpam_handle, PAM_CONV, (const void *)&null_conv);
	if (sshpam_cred_established) {
		pam_setcred(sshpam_handle, PAM_DELETE_CRED);
		sshpam_cred_established = 0;
	}
	if (sshpam_session_open) {
		pam_close_session(sshpam_handle, PAM_SILENT);
		sshpam_session_open = 0;
	}
	sshpam_authenticated = sshpam_new_authtok_reqd = 0;
	pam_end(sshpam_handle, sshpam_err);
	sshpam_handle = NULL;
}

static int
sshpam_init(Authctxt *authctxt)
{
	extern u_int utmp_len;
	const char *pam_rhost, *pam_user, *user = authctxt->user;

	if (sshpam_handle != NULL) {
		/* We already have a PAM context; check if the user matches */
		sshpam_err = pam_get_item(sshpam_handle,
		    PAM_USER, (const void **)&pam_user);
		if (sshpam_err == PAM_SUCCESS && strcmp(user, pam_user) == 0)
			return (0);
		pam_end(sshpam_handle, sshpam_err);
		sshpam_handle = NULL;
	}
	debug("PAM: initializing for \"%s\"", user);
	sshpam_err = pam_start("sshd", user, &null_conv, &sshpam_handle);
	if (sshpam_err != PAM_SUCCESS)
		return (-1);
	pam_rhost = get_remote_name_or_ip(utmp_len, options.use_dns);
	debug("PAM: setting PAM_RHOST to \"%s\"", pam_rhost);
	sshpam_err = pam_set_item(sshpam_handle, PAM_RHOST, pam_rhost);
	if (sshpam_err != PAM_SUCCESS) {
		pam_end(sshpam_handle, sshpam_err);
		sshpam_handle = NULL;
		return (-1);
	}
	return (0);
}

static void *
sshpam_init_ctx(Authctxt *authctxt)
{
	struct pam_ctxt *ctxt;
	int socks[2];

	/* Initialize PAM */
	if (sshpam_init(authctxt) == -1) {
		error("PAM: initialization failed");
		return (NULL);
	}

	ctxt = xmalloc(sizeof *ctxt);
	ctxt->pam_done = 0;

	/* Start the authentication thread */
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, socks) == -1) {
		error("PAM: failed create sockets: %s", strerror(errno));
		xfree(ctxt);
		return (NULL);
	}
	ctxt->pam_psock = socks[0];
	ctxt->pam_csock = socks[1];
	if (pthread_create(&ctxt->pam_thread, NULL, sshpam_thread, ctxt) == -1) {
		error("PAM: failed to start authentication thread: %s",
		    strerror(errno));
		close(socks[0]);
		close(socks[1]);
		xfree(ctxt);
		return (NULL);
	}
	cleanup_ctxt = ctxt;
	return (ctxt);
}

static int
sshpam_query(void *ctx, char **name, char **info,
    u_int *num, char ***prompts, u_int **echo_on)
{
	Buffer buffer;
	struct pam_ctxt *ctxt = ctx;
	size_t plen;
	u_char type;
	char *msg;

	buffer_init(&buffer);
	*name = xstrdup("");
	*info = xstrdup("");
	*prompts = xmalloc(sizeof(char *));
	**prompts = NULL;
	plen = 0;
	*echo_on = xmalloc(sizeof(u_int));
	while (ssh_msg_recv(ctxt->pam_psock, &buffer) == 0) {
		type = buffer_get_char(&buffer);
		msg = buffer_get_string(&buffer, NULL);
		switch (type) {
		case PAM_PROMPT_ECHO_ON:
		case PAM_PROMPT_ECHO_OFF:
			*num = 1;
			**prompts = xrealloc(**prompts, plen + strlen(msg) + 1);
			plen += sprintf(**prompts + plen, "%s", msg);
			**echo_on = (type == PAM_PROMPT_ECHO_ON);
			xfree(msg);
			return (0);
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			/* accumulate messages */
			**prompts = xrealloc(**prompts, plen + strlen(msg) + 1);
			plen += sprintf(**prompts + plen, "%s", msg);
			xfree(msg);
			break;
		case PAM_NEW_AUTHTOK_REQD:
			sshpam_new_authtok_reqd = 1;
			/* FALLTHROUGH */
		case PAM_SUCCESS:
		case PAM_AUTH_ERR:
			if (**prompts != NULL) {
				/* drain any accumulated messages */
#if 0 /* not compatible with privsep */
				packet_start(SSH2_MSG_USERAUTH_BANNER);
				packet_put_cstring(**prompts);
				packet_put_cstring("");
				packet_send();
				packet_write_wait();
#endif
				xfree(**prompts);
				**prompts = NULL;
			}
			if (type == PAM_SUCCESS) {
				*num = 0;
				**echo_on = 0;
				ctxt->pam_done = 1;
				xfree(msg);
				return (0);
			}
			error("PAM: %s", msg);
		default:
			*num = 0;
			**echo_on = 0;
			xfree(msg);
			ctxt->pam_done = -1;
			return (-1);
		}
	}
	return (-1);
}

static int
sshpam_respond(void *ctx, u_int num, char **resp)
{
	Buffer buffer;
	struct pam_ctxt *ctxt = ctx;
	char *msg;

	debug2("PAM: %s", __func__);
	switch (ctxt->pam_done) {
	case 1:
		sshpam_authenticated = 1;
		return (0);
	case 0:
		break;
	default:
		return (-1);
	}
	if (num != 1) {
		error("PAM: expected one response, got %u", num);
		return (-1);
	}
	buffer_init(&buffer);
	buffer_put_cstring(&buffer, *resp);
	ssh_msg_send(ctxt->pam_psock, PAM_AUTHTOK, &buffer);
	buffer_free(&buffer);
	return (1);
}

static void
sshpam_free_ctx(void *ctxtp)
{
	struct pam_ctxt *ctxt = ctxtp;

	sshpam_thread_cleanup();
	xfree(ctxt);
	/*
	 * We don't call pam_cleanup() here because we may need the PAM
	 * handle at a later stage, e.g. when setting up a session.  It's
	 * still on the cleanup list, so pam_end() *will* be called before
	 * the server process terminates.
	 */
}

KbdintDevice sshpam_device = {
	"pam",
	sshpam_init_ctx,
	sshpam_query,
	sshpam_respond,
	sshpam_free_ctx
};

KbdintDevice mm_sshpam_device = {
	"pam",
	mm_sshpam_init_ctx,
	mm_sshpam_query,
	mm_sshpam_respond,
	mm_sshpam_free_ctx
};

/*
 * This replaces auth-pam.c
 */
void
start_pam(Authctxt *authctxt)
{
	if (!options.use_pam)
		fatal("PAM: initialisation requested when UsePAM=no");

	if (sshpam_init(authctxt) == -1)
		fatal("PAM: initialisation failed");
}

void
finish_pam(void)
{
	sshpam_cleanup();
}

u_int
do_pam_account(void)
{
	/* XXX */
	return (1);
}

void
do_pam_set_tty(const char *tty)
{
	if (tty != NULL) {
		debug("PAM: setting PAM_TTY to \"%s\"", tty);
		sshpam_err = pam_set_item(sshpam_handle, PAM_TTY, tty);
		if (sshpam_err != PAM_SUCCESS)
			fatal("PAM: failed to set PAM_TTY: %s",
			    pam_strerror(sshpam_handle, sshpam_err));
	}
}

void
do_pam_session(void)
{
	sshpam_err = pam_set_item(sshpam_handle, PAM_CONV, (const void *)&null_conv);
	if (sshpam_err != PAM_SUCCESS)
		fatal("PAM: failed to set PAM_CONV: %s",
		    pam_strerror(sshpam_handle, sshpam_err));
	sshpam_err = pam_open_session(sshpam_handle, 0);
	if (sshpam_err != PAM_SUCCESS)
		fatal("PAM: pam_open_session(): %s",
		    pam_strerror(sshpam_handle, sshpam_err));
	sshpam_session_open = 1;
}

void
do_pam_setcred(int init)
{
	sshpam_err = pam_set_item(sshpam_handle, PAM_CONV, (const void *)&null_conv);
	if (sshpam_err != PAM_SUCCESS)
		fatal("PAM: failed to set PAM_CONV: %s",
		    pam_strerror(sshpam_handle, sshpam_err));
	if (init) {
		debug("PAM: establishing credentials");
		sshpam_err = pam_setcred(sshpam_handle, PAM_ESTABLISH_CRED);
	} else {
		debug("PAM: reinitializing credentials");
		sshpam_err = pam_setcred(sshpam_handle, PAM_REINITIALIZE_CRED);
	}
	if (sshpam_err == PAM_SUCCESS) {
		sshpam_cred_established = 1;
		return;
	}
	if (sshpam_authenticated)
		fatal("PAM: pam_setcred(): %s",
		    pam_strerror(sshpam_handle, sshpam_err));
	else
		debug("PAM: pam_setcred(): %s",
		    pam_strerror(sshpam_handle, sshpam_err));
}

int
is_pam_password_change_required(void)
{
	return (sshpam_new_authtok_reqd);
}

static int
sshpam_chauthtok_conv(int n,
	 const struct pam_message **msg,
	 struct pam_response **resp,
	 void *data)
{
	char input[PAM_MAX_MSG_SIZE];
	int i;

	if (n <= 0 || n > PAM_MAX_NUM_MSG)
		return (PAM_CONV_ERR);
	*resp = xmalloc(n * sizeof **resp);
	for (i = 0; i < n; ++i) {
		switch (msg[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
			resp[i]->resp =
			    read_passphrase(msg[i]->msg, RP_ALLOW_STDIN);
			resp[i]->resp_retcode = PAM_SUCCESS;
			break;
		case PAM_PROMPT_ECHO_ON:
			fputs(msg[i]->msg, stderr);
			fgets(input, sizeof input, stdin);
			resp[i]->resp = xstrdup(input);
			resp[i]->resp_retcode = PAM_SUCCESS;
			break;
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			fputs(msg[i]->msg, stderr);
			resp[i]->resp_retcode = PAM_SUCCESS;
			break;
		default:
			goto fail;
		}
	}
	return (PAM_SUCCESS);
 fail:
	while (i)
		xfree(resp[--i]);
	xfree(*resp);
	*resp = NULL;
	return (PAM_CONV_ERR);
}

/*
 * XXX this should be done in the authentication phase, but ssh1 doesn't
 * support that
 */
void
do_pam_chauthtok(void)
{
	struct pam_conv pam_conv = { sshpam_chauthtok_conv, NULL };

	if (use_privsep)
		fatal("PAM: chauthtok not supprted with privsep");
	sshpam_err = pam_set_item(sshpam_handle, PAM_CONV, (const void *)&pam_conv);
	if (sshpam_err != PAM_SUCCESS)
		fatal("PAM: failed to set PAM_CONV: %s",
		    pam_strerror(sshpam_handle, sshpam_err));
	debug("PAM: changing password");
	sshpam_err = pam_chauthtok(sshpam_handle, PAM_CHANGE_EXPIRED_AUTHTOK);
	if (sshpam_err != PAM_SUCCESS)
		fatal("PAM: pam_chauthtok(): %s",
		    pam_strerror(sshpam_handle, sshpam_err));
}

void
print_pam_messages(void)
{
	/* XXX */
}

char **
fetch_pam_child_environment(void)
{
	/* XXX */
	return (NULL);
}

char **
fetch_pam_environment(void)
{
#ifdef HAVE_PAM_GETENVLIST
	debug("PAM: retrieving environment");
	return (pam_getenvlist(sshpam_handle));
#else
	return (NULL);
#endif
}

void
free_pam_environment(char **env)
{
	char **envp;

	if (env == NULL)
		return;

	for (envp = env; *envp; envp++)
		xfree(*envp);
	xfree(env);
}

/*
 * "Blind" conversation function for password authentication.  Assumes that
 * echo-off prompts are for the password and stores messages for later
 * display.
 */
static int
sshpam_passwd_conv(int n, struct pam_message **msg,
    struct pam_response **resp, void *data)
{
        struct pam_response *reply;
        int i;
        size_t len;

        debug3("PAM: %s called with %d messages", __func__, n);

        *resp = NULL;

        if (n <= 0 || n > PAM_MAX_NUM_MSG)
                return (PAM_CONV_ERR);

        if ((reply = malloc(n * sizeof(*reply))) == NULL)
                return (PAM_CONV_ERR);
        memset(reply, 0, n * sizeof(*reply));

        for (i = 0; i < n; ++i) {
                switch (PAM_MSG_MEMBER(msg, i, msg_style)) {
                case PAM_PROMPT_ECHO_OFF:
                        if (sshpam_password == NULL)
                                goto fail;
                        if ((reply[i].resp = strdup(sshpam_password)) == NULL)
                                goto fail;
                        reply[i].resp_retcode = PAM_SUCCESS;
                        break;
                case PAM_ERROR_MSG:
                case PAM_TEXT_INFO:
                        len = strlen(PAM_MSG_MEMBER(msg, i, msg));
                        if (len > 0) {
                                buffer_append(&loginmsg,
                                    PAM_MSG_MEMBER(msg, i, msg), len);
                                buffer_append(&loginmsg, "\n", 1);
                        }
                        if ((reply[i].resp = strdup("")) == NULL)
                                goto fail;
                        reply[i].resp_retcode = PAM_SUCCESS;
                        break;
                default:
                        goto fail;
                }
        }
        *resp = reply;
        return (PAM_SUCCESS);

 fail:
        for(i = 0; i < n; i++) {
                if (reply[i].resp != NULL)
                        xfree(reply[i].resp);
        }
        xfree(reply);
        return (PAM_CONV_ERR);
}

static struct pam_conv passwd_conv = { sshpam_passwd_conv, NULL };

sshpam_auth_passwd(Authctxt *authctxt, const char *password)
{
        int flags = (options.permit_empty_passwd == 0 ?
            PAM_DISALLOW_NULL_AUTHTOK : 0);
        static char badpw[] = "\b\n\r\177INCORRECT";

        if (!options.use_pam || sshpam_handle == NULL)
                fatal("PAM: %s called when PAM disabled or failed to "
                    "initialise.", __func__);

        sshpam_password = password;
        sshpam_authctxt = authctxt;

        /*
         * If the user logging in is invalid, or is root but is not permitted
         * by PermitRootLogin, use an invalid password to prevent leaking
         * information via timing (eg if the PAM config has a delay on fail).
         */
        if (!authctxt->valid || (authctxt->pw->pw_uid == 0 &&
             options.permit_root_login != PERMIT_YES))
                sshpam_password = badpw;

        sshpam_err = pam_set_item(sshpam_handle, PAM_CONV,
            (const void *)&passwd_conv);
        if (sshpam_err != PAM_SUCCESS)
                fatal("PAM: %s: failed to set PAM_CONV: %s", __func__,
                    pam_strerror(sshpam_handle, sshpam_err));

        sshpam_err = pam_authenticate(sshpam_handle, flags);
        sshpam_password = NULL;
        if (sshpam_err == PAM_SUCCESS && authctxt->valid) {
                debug("PAM: password authentication accepted for %.100s",
                    authctxt->user);
               return 1;
        } else {
                debug("PAM: password authentication failed for %.100s: %s",
                    authctxt->valid ? authctxt->user : "an illegal user",
                    pam_strerror(sshpam_handle, sshpam_err));
                return 0;
        }
}

#endif /* USE_PAM */
