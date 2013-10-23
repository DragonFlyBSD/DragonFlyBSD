/*-
 * Copyright (c) 1999, 2000 Robert N. M. Watson
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
 * $FreeBSD: src/sys/kern/kern_acl.c,v 1.2.2.1 2000/07/28 18:48:16 rwatson Exp $
 * $DragonFly: src/sys/kern/kern_acl.c,v 1.17 2007/02/19 00:51:54 swildner Exp $
 */

/*
 * Generic routines to support file system ACLs, at a syntactic level
 * Semantics are the responsibility of the underlying file system
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/nlookup.h>
#include <sys/file.h>
#include <sys/sysent.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/acl.h>

#include <sys/mplock2.h>

static int vacl_set_acl(struct vnode *vp, acl_type_t type, struct acl *aclp);
static int vacl_get_acl(struct vnode *vp, acl_type_t type, struct acl *aclp);
static int vacl_aclcheck(struct vnode *vp, acl_type_t type, struct acl *aclp);

/*
 * These calls wrap the real vnode operations, and are called by the 
 * syscall code once the syscall has converted the path or file
 * descriptor to a vnode (unlocked).  The aclp pointer is assumed
 * still to point to userland, so this should not be consumed within
 * the kernel except by syscall code.  Other code should directly
 * invoke VOP_{SET,GET}ACL.
 */

/*
 * Given a vnode, set its ACL.
 */
static int
vacl_set_acl(struct vnode *vp, acl_type_t type, struct acl *aclp)
{
	struct thread *td = curthread;
	struct acl inkernacl;
	struct ucred *ucred;
	int error;

	error = copyin(aclp, &inkernacl, sizeof(struct acl));
	if (error)
		return(error);
	ucred = td->td_ucred;

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_SETACL(vp, type, &inkernacl, ucred);
	vn_unlock(vp);
	return(error);
}

/*
 * Given a vnode, get its ACL.
 */
static int
vacl_get_acl(struct vnode *vp, acl_type_t type, struct acl *aclp)
{
	struct thread *td = curthread;
	struct acl inkernelacl;
	struct ucred *ucred;
	int error;

	ucred = td->td_ucred;
	error = VOP_GETACL(vp, type, &inkernelacl, ucred);
	if (error == 0)
		error = copyout(&inkernelacl, aclp, sizeof(struct acl));
	return (error);
}

/*
 * Given a vnode, delete its ACL.
 */
static int
vacl_delete(struct vnode *vp, acl_type_t type)
{
	struct thread *td = curthread;
	struct ucred *ucred;
	int error;

	ucred = td->td_ucred;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_SETACL(vp, ACL_TYPE_DEFAULT, 0, ucred);
	vn_unlock(vp);
	return (error);
}

/*
 * Given a vnode, check whether an ACL is appropriate for it
 */
static int
vacl_aclcheck(struct vnode *vp, acl_type_t type, struct acl *aclp)
{
	struct thread *td = curthread;
	struct ucred *ucred;
	struct acl inkernelacl;
	int error;

	ucred = td->td_ucred;
	error = copyin(aclp, &inkernelacl, sizeof(struct acl));
	if (error)
		return(error);
	error = VOP_ACLCHECK(vp, type, &inkernelacl, ucred);
	return (error);
}

/*
 * syscalls -- convert the path/fd to a vnode, and call vacl_whatever.
 * Don't need to lock, as the vacl_ code will get/release any locks
 * required.
 */

/*
 * Given a file path, get an ACL for it
 */
int
sys___acl_get_file(struct __acl_get_file_args *uap)
{
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
	nlookup_done(&nd);
	if (error == 0) {
		error = vacl_get_acl(vp, uap->type, uap->aclp);
		vrele(vp);
	}
	return (error);
}

/*
 * Given a file path, set an ACL for it
 */
int
sys___acl_set_file(struct __acl_set_file_args *uap)
{
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
	nlookup_done(&nd);
	if (error == 0) {
		error = vacl_set_acl(vp, uap->type, uap->aclp);
		vrele(vp);
	}
	return (error);
}

/*
 * Given a file descriptor, get an ACL for it
 */
int
sys___acl_get_fd(struct __acl_get_fd_args *uap)
{
	struct thread *td = curthread;
	struct file *fp;
	int error;

	KKASSERT(td->td_proc);
	if ((error = holdvnode(td->td_proc->p_fd, uap->filedes, &fp)) != 0)
		return(error);
	error = vacl_get_acl((struct vnode *)fp->f_data, uap->type, uap->aclp);
	fdrop(fp);

	return (error);
}

/*
 * Given a file descriptor, set an ACL for it
 */
int
sys___acl_set_fd(struct __acl_set_fd_args *uap)
{
	struct thread *td = curthread;
	struct file *fp;
	int error;

	KKASSERT(td->td_proc);
	if ((error = holdvnode(td->td_proc->p_fd, uap->filedes, &fp)) != 0)
		return(error);
	error = vacl_set_acl((struct vnode *)fp->f_data, uap->type, uap->aclp);
	fdrop(fp);
	return (error);
}

/*
 * Given a file path, delete an ACL from it.
 */
int
sys___acl_delete_file(struct __acl_delete_file_args *uap)
{
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
	nlookup_done(&nd);

	if (error == 0) {
		error = vacl_delete(vp, uap->type);
		vrele(vp);
	}
	return (error);
}

/*
 * Given a file path, delete an ACL from it.
 */
int
sys___acl_delete_fd(struct __acl_delete_fd_args *uap)
{
	struct thread *td = curthread;
	struct file *fp;
	int error;

	KKASSERT(td->td_proc);
	if ((error = holdvnode(td->td_proc->p_fd, uap->filedes, &fp)) != 0)
		return(error);
	error = vacl_delete((struct vnode *)fp->f_data, uap->type);
	fdrop(fp);
	return (error);
}

/*
 * Given a file path, check an ACL for it
 */
int
sys___acl_aclcheck_file(struct __acl_aclcheck_file_args *uap)
{
	struct nlookupdata nd;
	struct vnode *vp;
	int error;

	vp = NULL;
	error = nlookup_init(&nd, uap->path, UIO_USERSPACE, NLC_FOLLOW);
	if (error == 0)
		error = nlookup(&nd);
	if (error == 0)
		error = cache_vref(&nd.nl_nch, nd.nl_cred, &vp);
	nlookup_done(&nd);

	if (error == 0) {
		error = vacl_aclcheck(vp, uap->type, uap->aclp);
		vrele(vp);
	}
	return (error);
}

/*
 * Given a file descriptor, check an ACL for it
 */
int
sys___acl_aclcheck_fd(struct __acl_aclcheck_fd_args *uap)
{
	struct thread *td = curthread;
	struct file *fp;
	int error;

	KKASSERT(td->td_proc);
	if ((error = holdvnode(td->td_proc->p_fd, uap->filedes, &fp)) != 0)
		return(error);
	error = vacl_aclcheck((struct vnode *)fp->f_data, uap->type, uap->aclp);
	fdrop(fp);
	return (error);
}

