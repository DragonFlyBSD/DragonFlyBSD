/*
 * Copyright (c) 2013 Larisa Grigore <larisagrigore@gmail.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Herb Peyerl.
 * 4. The name of Herb Peyerl may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>

#include "perm.h"
#include <errno.h>

static int
is_root(struct cmsgcred *cred) {
	return (cred->cmcred_euid == 0);
}

static int
is_grpmember(gid_t gid, struct cmsgcred *cred) {
	int n;

	if (cred->cmcred_gid == gid)
		return (1);

	for (n = 0 ; n < cred->cmcred_ngroups ; n++) {
		if (cred->cmcred_groups[n] == gid)
			return (1);
	}

	return (0);
}

int
ipcperm(struct cmsgcred *cred, struct ipc_perm *perm, int mode) {
	if (cred == NULL)
		return (0);

	if (cred->cmcred_euid != perm->cuid
			&& cred->cmcred_euid != perm->uid) {
		/* In order to modify control info the caller must be
		 * owner, creator or privileged.
		 */
		if (mode & IPC_M)
			return (is_root(cred) ? 0 : EACCES);

		/* Check for group match. */
		mode >>= 3;
		if (!is_grpmember(perm->gid, cred) &&
				!is_grpmember(perm->cgid, cred))
			mode >>= 3;
	}

	if (mode & IPC_M)
		return (0);

	if ((mode & perm->mode) == mode)
		return (0);

	if (is_root(cred))
		return (0);

	return (EACCES);
}
