/*-
 * Copyright (c) 1994-1995 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 *
 * $FreeBSD: src/sys/i386/linux/linux_dummy.c,v 1.21.2.7 2003/01/02 20:41:33 kan Exp $
 * $DragonFly: src/sys/emulation/linux/i386/linux_dummy.c,v 1.7 2006/06/05 07:26:10 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include "linux.h"
#include "linux_proto.h"
#include "../linux_util.h"

DUMMY(stat);
DUMMY(mount);
DUMMY(stime);
DUMMY(fstat);
DUMMY(olduname);
DUMMY(syslog);
DUMMY(uname);
DUMMY(vhangup);
DUMMY(vm86old);
DUMMY(swapoff);
DUMMY(adjtimex);
DUMMY(create_module);
DUMMY(init_module);
DUMMY(delete_module);
DUMMY(get_kernel_syms);
DUMMY(quotactl);
DUMMY(bdflush);
DUMMY(sysfs);
DUMMY(vm86);
DUMMY(query_module);
DUMMY(nfsservctl);
DUMMY(prctl);
DUMMY(rt_sigpending);
DUMMY(rt_sigtimedwait);
DUMMY(rt_sigqueueinfo);
DUMMY(capget);
DUMMY(capset);
DUMMY(sendfile);
DUMMY(setfsuid);
DUMMY(setfsgid);
DUMMY(pivot_root);
DUMMY(mincore);
DUMMY(fadvise64);
DUMMY(statfs64);
DUMMY(fstatfs64);
DUMMY(fadvise64_64);
DUMMY(mbind);
DUMMY(get_mempolicy);
DUMMY(set_mempolicy);
DUMMY(kexec_load);
DUMMY(waitid);
DUMMY(add_key);
DUMMY(request_key);
DUMMY(keyctl);
DUMMY(ioprio_set);
DUMMY(ioprio_get);
DUMMY(inotify_init);
DUMMY(inotify_add_watch);
DUMMY(inotify_rm_watch);
DUMMY(migrate_pages);
DUMMY(pselect6);
DUMMY(ppoll);
DUMMY(unshare);
DUMMY(splice);
DUMMY(sync_file_range);
DUMMY(tee);
DUMMY(vmsplice);
DUMMY(move_pages);
DUMMY(epoll_pwait);
DUMMY(signalfd);
DUMMY(timerfd);
DUMMY(eventfd);

#define DUMMY_XATTR(s)						\
int								\
sys_linux_ ## s ## xattr( struct linux_ ## s ## xattr_args *arg)	\
{								\
	return (ENOATTR);					\
}

DUMMY_XATTR(set);
DUMMY_XATTR(lset);
DUMMY_XATTR(fset);
DUMMY_XATTR(get);
DUMMY_XATTR(lget);
DUMMY_XATTR(fget);
DUMMY_XATTR(list);
DUMMY_XATTR(llist);
DUMMY_XATTR(flist);
DUMMY_XATTR(remove);
DUMMY_XATTR(lremove);
DUMMY_XATTR(fremove);
