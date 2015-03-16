/*
 * These macros are partially based on Linux-PAM's <security/_pam_macros.h>,
 * which were organized by Cristian Gafton and I believe are in the public
 * domain.
 */

#ifndef PAM_PASSWDQC_MACROS_H__
#define PAM_PASSWDQC_MACROS_H__

#include <string.h>
#include <stdlib.h>

static __inline void
pwqc_overwrite_string(char *x)
{
	if (x)
		memset(x, 0, strlen(x));
}

static __inline void
pwqc_drop_mem(void *x)
{
	if (x) {
		free(x);
		x = NULL;
	}
}

static __inline void
pwqc_drop_pam_reply(struct pam_response *reply, int replies)
{
	if (reply) {
		int reply_i;

		for (reply_i = 0; reply_i < replies; ++reply_i) {
			pwqc_overwrite_string(reply[reply_i].resp);
			pwqc_drop_mem(reply[reply_i].resp);
		}
		pwqc_drop_mem(reply);
	}
}

#endif /* PAM_PASSWDQC_MACROS_H__ */
