/*-
 * Copyright (c) 1993, David Greenman
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/sys/imgact.h,v 1.22.2.2 2001/12/22 01:21:44 jwd Exp $
 * $DragonFly: src/sys/sys/imgact.h,v 1.6 2004/04/11 00:10:34 dillon Exp $
 */

#ifndef _SYS_IMGACT_H_
#define	_SYS_IMGACT_H_

#include <sys/mount.h>
#include <cpu/lwbuf.h>

#define MAXSHELLCMDLEN	128

struct image_args {
	char *buf;		/* pointer to string buffer */
	char *begin_argv;	/* beginning of argv in buf */
	char *begin_envv;	/* beginning of envv in buf */
	char *endp;		/* current `end' pointer of arg & env strings */
	char *fname;		/* beginning of file name */
	int space;		/* space left in arg & env buffer */
	int argc;		/* count of argument strings */
	int envc;		/* count of environment strings */
};

struct image_params {
	struct proc *proc;	/* our process struct */
	struct image_args *args;	/* syscall arguments */
	struct vnode *vp;	/* pointer to vnode of file to exec */
	struct vattr_lite *lvap; /* attributes of file */
	const char *image_header; /* head of file to exec */
	unsigned long entry_addr; /* entry address of target executable */
	char resident;		/* flag - resident image */
	char vmspace_destroyed;	/* flag - we've blown away original vm space */
	char interpreted;	/* flag - this executable is interpreted */
	char interpreter_name[MAXSHELLCMDLEN]; /* name of the interpreter */
	void *auxargs;		/* ELF Auxinfo structure pointer */
	struct lwbuf *firstpage;	/* first page that we mapped */
	struct lwbuf firstpage_cache;
	unsigned long ps_strings; /* PS_STRINGS for BSD/OS binaries */
	char *execpath;
	unsigned long execpathp;
	char *freepath;
};

#ifdef _KERNEL
enum	exec_path_segflg {PATH_SYSSPACE, PATH_USERSPACE};

struct vmspace;
int	exec_resident_imgact (struct image_params *);
int	exec_check_permissions (struct image_params *, struct mount *);
int	exec_new_vmspace (struct image_params *, struct vmspace *vmres);
int	exec_shell_imgact (struct image_params *);
int	exec_copyin_args(struct image_args *, char *, enum exec_path_segflg,
	char **, char **);
void	exec_free_args(struct image_args *);
#endif

#endif /* !_SYS_IMGACT_H_ */
