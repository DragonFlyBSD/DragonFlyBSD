/*-
 * Copyright (c) 2003 Kip Macy All rights reserved.
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
 * $DragonFly: src/sys/checkpt/Attic/support.c,v 1.1 2003/10/20 04:48:42 dillon Exp $
 */

/* prune the includes XXX */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/module.h>
#include <sys/sysent.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/file.h>
/* only on dragonfly */
#include <sys/file2.h>
#include <sys/fcntl.h>
#include <sys/signal.h>
#include <vm/vm.h>
#include <sys/imgact_elf.h>
#include <sys/procfs.h>

#include <sys/lock.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <sys/mman.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <machine/limits.h>
#include <i386/include/frame.h>
#include <sys/signalvar.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <i386/include/sigframe.h>
#include <sys/exec.h>
#include <sys/unistd.h>
#include <sys/time.h>
#include "checkpt.h"


static char ckptfilename[64] = {"%N.ckpt"};
SYSCTL_STRING(_kern, OID_AUTO, ckptfile, CTLFLAG_RW, ckptfilename,
	      sizeof(ckptfilename), "process checkpoint name format string");

/*
 * expand_name(name, uid, pid)
 * Expand the name described in corefilename, using name, uid, and pid.
 * corefilename is a printf-like string, with three format specifiers:
 *	%N	name of process ("name")
 *	%P	process id (pid)
 *	%U	user id (uid)
 * For example, "%N.core" is the default; they can be disabled completely
 * by using "/dev/null", or all core files can be stored in "/cores/%U/%N-%P".
 * This is controlled by the sysctl variable kern.corefile (see above).
 *
 * -- taken from the coredump code
 */

char *
ckpt_expand_name(const char *name, uid_t uid, pid_t pid)
{
	char *temp;
	char buf[11];		/* Buffer for pid/uid -- max 4B */
	int i, n;
	char *format = ckptfilename;
	size_t namelen;

	temp = malloc(MAXPATHLEN + 1, M_TEMP, M_NOWAIT);
	if (temp == NULL)
		return NULL;
	namelen = strlen(name);
	for (i = 0, n = 0; n < MAXPATHLEN && format[i]; i++) {
		int l;
		switch (format[i]) {
		case '%':	/* Format character */
			i++;
			switch (format[i]) {
			case '%':
				temp[n++] = '%';
				break;
			case 'N':	/* process name */
				if ((n + namelen) > MAXPATHLEN) {
					log(LOG_ERR, "pid %d (%s), uid (%u):  Path `%s%s' is too long\n",
					    pid, name, uid, temp, name);
					free(temp, M_TEMP);
					return NULL;
				}
				memcpy(temp+n, name, namelen);
				n += namelen;
				break;
			case 'P':	/* process id */
				l = sprintf(buf, "%u", pid);
				if ((n + l) > MAXPATHLEN) {
					log(LOG_ERR, "pid %d (%s), uid (%u):  Path `%s%s' is too long\n",
					    pid, name, uid, temp, name);
					free(temp, M_TEMP);
					return NULL;
				}
				memcpy(temp+n, buf, l);
				n += l;
				break;
			case 'U':	/* user id */
				l = sprintf(buf, "%u", uid);
				if ((n + l) > MAXPATHLEN) {
					log(LOG_ERR, "pid %d (%s), uid (%u):  Path `%s%s' is too long\n",
					    pid, name, uid, temp, name);
					free(temp, M_TEMP);
					return NULL;
				}
				memcpy(temp+n, buf, l);
				n += l;
				break;
			default:
			  	log(LOG_ERR, "Unknown format character %c in `%s'\n", format[i], format);
			}
			break;
		default:
			temp[n++] = format[i];
		}
	}
	temp[n] = '\0';
	return temp;
}

