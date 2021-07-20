/*
 * These macros are partially based on Linux-PAM's <security/_pam_macros.h>,
 * which were organized by Cristian Gafton and I believe are in the public
 * domain.
 *
 * - Solar Designer
 */

#ifndef PAM_PASSWDQC_MACROS_H__
#define PAM_PASSWDQC_MACROS_H__

#include <string.h>
#include <stdlib.h>

#define pwqc_overwrite_string(x) \
do { \
	if (x) \
		memset((x), 0, strlen(x)); \
} while (0)

#define pwqc_drop_mem(x) \
do { \
	if (x) { \
		free(x); \
		(x) = NULL; \
	} \
} while (0)

#define pwqc_drop_pam_reply(/* struct pam_response* */ reply, /* int */ replies) \
do { \
	if (reply) { \
		int reply_i; \
\
		for (reply_i = 0; reply_i < (replies); ++reply_i) { \
			pwqc_overwrite_string((reply)[reply_i].resp); \
			pwqc_drop_mem((reply)[reply_i].resp); \
		} \
		pwqc_drop_mem(reply); \
	} \
} while (0)

#endif /* PAM_PASSWDQC_MACROS_H__ */
