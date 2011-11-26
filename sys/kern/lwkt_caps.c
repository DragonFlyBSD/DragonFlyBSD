/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/kern/lwkt_caps.c,v 1.13 2007/02/26 21:41:08 corecode Exp $
 */

/*
 * This module implements the DragonFly LWKT IPC rendezvous and message
 * passing API which operates between userland processes, between userland
 * threads, and between userland processes and kernel threads.  This API
 * is known as the CAPS interface.
 *
 * Generally speaking this module abstracts the LWKT message port interface
 * into userland Clients and Servers rendezvous through ports named
 * by or wildcarded by (name,uid,gid).  The kernel provides system calls 
 * which may be assigned to the mp_* fields in a userland-supplied
 * kernel-managed port, and a registration interface which associates an
 * upcall with a userland port.  The kernel tracks authentication information
 * and deals with connection failures by automatically replying to unreplied
 * messages.
 *
 * From the userland perspective a client/server connection involves two
 * message ports on the client and two message ports on the server.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/ucred.h>
#include <sys/caps.h>
#include <sys/sysctl.h>

#include <sys/mplock2.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

static int caps_process_msg(caps_kinfo_t caps, caps_kmsg_t msg, struct caps_sys_get_args *uap);
static void caps_free(caps_kinfo_t caps);
static void caps_free_msg(caps_kmsg_t msg);
static int caps_name_check(const char *name, size_t len);
static caps_kinfo_t caps_free_msg_mcaps(caps_kmsg_t msg);
static caps_kinfo_t kern_caps_sys_service(const char *name, uid_t uid, 
			gid_t gid, struct ucred *cred, 
			int flags, int *error);
static caps_kinfo_t kern_caps_sys_client(const char *name, uid_t uid,
			gid_t gid, struct ucred *cred, int flags, int *error);

#define CAPS_HSIZE	64
#define CAPS_HMASK	(CAPS_HSIZE - 1)

static caps_kinfo_t	caps_hash_ary[CAPS_HSIZE];
static int caps_waitsvc;

MALLOC_DEFINE(M_CAPS, "caps", "caps IPC messaging");

static int caps_enabled;
SYSCTL_INT(_kern, OID_AUTO, caps_enabled,
        CTLFLAG_RW, &caps_enabled, 0, "Enable CAPS");

/************************************************************************
 *			INLINE SUPPORT FUNCTIONS			*
 ************************************************************************/

static __inline
struct caps_kinfo **
caps_hash(const char *name, int len)
{
    int hv = 0x7123F4B3;

    while (--len >= 0)
	hv = (hv << 5) ^ name[len] ^ (hv >> 23);
    return(&caps_hash_ary[(hv ^ (hv >> 16)) & CAPS_HMASK]);
}

static __inline
void
caps_hold(caps_kinfo_t caps)
{
    ++caps->ci_refs;
}

static __inline
void
caps_drop(caps_kinfo_t caps)
{
    if (--caps->ci_refs == 0)
	caps_free(caps);
}

/************************************************************************
 *			STATIC SUPPORT FUNCTIONS			*
 ************************************************************************/

static
caps_kinfo_t
caps_find(const char *name, int len, uid_t uid, gid_t gid)
{
    caps_kinfo_t caps;
    struct caps_kinfo **chash;

    chash = caps_hash(name, len);
    for (caps = *chash; caps; caps = caps->ci_hnext) {
	if ((uid == (uid_t)-1 || uid == caps->ci_uid) &&
	    (gid == (gid_t)-1 || gid == caps->ci_gid) &&
	    len == caps->ci_namelen &&
	    bcmp(name, caps->ci_name, len) == 0
	) {
	    caps_hold(caps);
	    break;
	}
    }
    return(caps);
}

static
caps_kinfo_t
caps_find_id(thread_t td, int id)
{
   caps_kinfo_t caps;

   for (caps = td->td_caps; caps; caps = caps->ci_tdnext) {
	if (caps->ci_id == id) {
	    caps_hold(caps);
	    break;
	}
   }
   return(caps);
}

static
caps_kinfo_t
caps_alloc(thread_t td, const char *name, int len, uid_t uid, gid_t gid, 
	    int flags, caps_type_t type)
{
    struct caps_kinfo **chash;
    caps_kinfo_t caps;
    caps_kinfo_t ctmp;

    caps = kmalloc(offsetof(struct caps_kinfo, ci_name[len+1]), 
			M_CAPS, M_WAITOK|M_ZERO);
    TAILQ_INIT(&caps->ci_msgpendq);
    TAILQ_INIT(&caps->ci_msguserq);
    caps->ci_uid = uid;		/* -1 == not registered for uid search */
    caps->ci_gid = gid;		/* -1 == not registered for gid search */
    caps->ci_type = type;
    caps->ci_refs = 1;	/* CAPKF_TDLIST reference */
    caps->ci_namelen = len;
    caps->ci_flags = flags;
    bcopy(name, caps->ci_name, len + 1);
    if (type == CAPT_SERVICE) {
	chash = caps_hash(caps->ci_name, len);
	caps->ci_hnext = *chash;
	*chash = caps;
	caps->ci_flags |= CAPKF_HLIST;
    }
    if (td->td_caps) {
	caps->ci_id = td->td_caps->ci_id + 1;
	if (caps->ci_id < 0) {
	    /*
	     * It is virtually impossible for this case to occur.
	     */
	    caps->ci_id = 1;
	    while ((ctmp = caps_find_id(td, caps->ci_id)) != NULL) {
		caps_drop(ctmp);
		++caps->ci_id;
	    }
	}
    } else {
	caps->ci_id = 1;
    }
    caps->ci_flags |= CAPKF_TDLIST;
    caps->ci_tdnext = td->td_caps;
    caps->ci_td = td;
    td->td_caps = caps;
    return(caps);
}

static
caps_kmsg_t
caps_alloc_msg(caps_kinfo_t caps)
{
    caps_kmsg_t msg;

    msg = kmalloc(sizeof(struct caps_kmsg), M_CAPS, M_WAITOK|M_ZERO);
    msg->km_msgid.c_id = (off_t)(uintptr_t)msg;
    return(msg);
}

static
caps_kmsg_t
caps_find_msg(caps_kinfo_t caps, off_t msgid)
{
    caps_kmsg_t msg;

    TAILQ_FOREACH(msg, &caps->ci_msguserq, km_node) {
	if (msg->km_msgid.c_id == msgid)
	    return(msg);
    }
    TAILQ_FOREACH(msg, &caps->ci_msgpendq, km_node) {
	if (msg->km_msgid.c_id == msgid)
	    return(msg);
    }
    return(NULL);
}

static
caps_kinfo_t
caps_load_ccr(caps_kinfo_t caps, caps_kmsg_t msg, struct lwp *lp,
	      void *udata, int ubytes)
{
    struct ucred *cr = lp ? lp->lwp_thread->td_ucred : proc0.p_ucred;
    caps_kinfo_t rcaps;
    int i;

    /*
     * replace km_mcaps with new VM state, return the old km_mcaps.  The
     * caller is expected to drop the rcaps ref count on return so we do
     * not do it ourselves.
     */
    rcaps = caps_free_msg_mcaps(msg);	/* can be NULL */
    caps_hold(caps);
    msg->km_mcaps = caps;
    xio_init_ubuf(&msg->km_xio, udata, ubytes, XIOF_READ);

    msg->km_ccr.pid = lp ? lp->lwp_proc->p_pid : -1;
    msg->km_ccr.uid = cr->cr_ruid;
    msg->km_ccr.euid = cr->cr_uid;
    msg->km_ccr.gid = cr->cr_rgid;
    msg->km_ccr.ngroups = MIN(cr->cr_ngroups, CAPS_MAXGROUPS);
    for (i = 0; i < msg->km_ccr.ngroups; ++i)
	msg->km_ccr.groups[i] = cr->cr_groups[i];
    return(rcaps);
}

static void
caps_dequeue_msg(caps_kinfo_t caps, caps_kmsg_t msg)
{
    if (msg->km_flags & CAPKMF_ONUSERQ)
	TAILQ_REMOVE(&caps->ci_msguserq, msg, km_node);
    if (msg->km_flags & CAPKMF_ONPENDQ)
	TAILQ_REMOVE(&caps->ci_msgpendq, msg, km_node);
    msg->km_flags &= ~(CAPKMF_ONPENDQ|CAPKMF_ONUSERQ);
}

static void
caps_put_msg(caps_kinfo_t caps, caps_kmsg_t msg, caps_msg_state_t state)
{
    KKASSERT((msg->km_flags & (CAPKMF_ONUSERQ|CAPKMF_ONPENDQ)) == 0);

    msg->km_flags |= CAPKMF_ONPENDQ;
    msg->km_flags &= ~CAPKMF_PEEKED;
    msg->km_state = state;
    TAILQ_INSERT_TAIL(&caps->ci_msgpendq, msg, km_node);

    /*
     * Instead of waking up the service for both new messages and disposals,
     * just wakeup the service for new messages and it will process the
     * previous disposal in the same loop, reducing the number of context
     * switches required to run an IPC.
     */
    if (state != CAPMS_DISPOSE)
	wakeup(caps);
    caps_drop(caps);
}

/*
 * caps_free_msg_mcaps()
 */
static
caps_kinfo_t
caps_free_msg_mcaps(caps_kmsg_t msg)
{
    caps_kinfo_t mcaps;

    mcaps = msg->km_mcaps;	/* may be NULL */
    msg->km_mcaps = NULL;
    if (msg->km_xio.xio_npages)
	xio_release(&msg->km_xio);
    return(mcaps);
}

/*
 * caps_free_msg()
 *
 * Free a caps placeholder message.  The message must not be on any queues.
 */
static void
caps_free_msg(caps_kmsg_t msg)
{
    caps_kinfo_t rcaps;

    if ((rcaps = caps_free_msg_mcaps(msg)) != NULL)
	caps_drop(rcaps);
    kfree(msg, M_CAPS);
}

/*
 * Validate the service name
 */
static int
caps_name_check(const char *name, size_t len)
{
    size_t i;
    char c;

    for (i = len - 1; i >= 0; --i) {
	c = name[i];
	if (c >= '0' && c <= '9')
	    continue;
	if (c >= 'a' && c <= 'z')
	    continue;
	if (c >= 'A' && c <= 'Z')
	    continue;
	if (c == '_' || c == '.')
	    continue;
	return(EINVAL);
    }
    return(0);
}

/*
 * caps_term()
 *
 * Terminate portions of a caps info structure.  This is used to close
 * an end-point or to flush particular messages on an end-point.
 *
 * This function should not be called with CAPKF_TDLIST unless the caller
 * has an additional hold on the caps structure.
 */
static void
caps_term(caps_kinfo_t caps, int flags, caps_kinfo_t cflush)
{
    struct thread *td = curthread;
    struct caps_kinfo **scan;
    caps_kmsg_t msg;

    if (flags & CAPKF_TDLIST)
	caps->ci_flags |= CAPKF_CLOSED;

    if (flags & CAPKF_FLUSH) {
	int mflags;
	struct caps_kmsg_queue tmpuserq;
	struct caps_kmsg_queue tmppendq;
	caps_kinfo_t rcaps;

	TAILQ_INIT(&tmpuserq);
	TAILQ_INIT(&tmppendq);

	while ((msg = TAILQ_FIRST(&caps->ci_msgpendq)) != NULL ||
	       (msg = TAILQ_FIRST(&caps->ci_msguserq)) != NULL
	) {
	    mflags = msg->km_flags & (CAPKMF_ONUSERQ|CAPKMF_ONPENDQ);
	    caps_dequeue_msg(caps, msg);

	    if (cflush && msg->km_mcaps != cflush) {
		if (mflags & CAPKMF_ONUSERQ)
		    TAILQ_INSERT_TAIL(&tmpuserq, msg, km_node);
		else
		    TAILQ_INSERT_TAIL(&tmppendq, msg, km_node);
	    } else {
		/*
		 * Dispose of the message.  If the received message is a
		 * request we must reply it.  If the received message is
		 * a reply we must return it for disposal.  If the
		 * received message is a disposal request we simply free it.
		 */
		switch(msg->km_state) {
		case CAPMS_REQUEST:
		case CAPMS_REQUEST_RETRY:
		    rcaps = caps_load_ccr(caps, msg, td->td_lwp, NULL, 0);
		    if (rcaps->ci_flags & CAPKF_CLOSED) {
			/*
			 * can't reply, if we never read the message (its on
			 * the pending queue), or if we are closed ourselves,
			 * we can just free the message.  Otherwise we have
			 * to send ourselves a disposal request (multi-threaded
			 * services have to deal with disposal requests for
			 * messages that might be in progress).
			 */
			if ((caps->ci_flags & CAPKF_CLOSED) ||
			    (mflags & CAPKMF_ONPENDQ)
			) {
			    caps_free_msg(msg);
			    caps_drop(rcaps);
			} else {
			    caps_drop(rcaps);
			    caps_hold(caps);	/* for message */
			    caps_put_msg(caps, msg, CAPMS_DISPOSE);
			}
		    } else {
			/*
			 * auto-reply to the originator.  rcaps already
			 * has a dangling hold so we do not have to hold it
			 * again.
			 */
			caps_put_msg(rcaps, msg, CAPMS_REPLY);
		    }
		    break;
		case CAPMS_REPLY:
		case CAPMS_REPLY_RETRY:
		    rcaps = caps_load_ccr(caps, msg, td->td_lwp, NULL, 0);
		    if (caps == rcaps || (rcaps->ci_flags & CAPKF_CLOSED)) {
			caps_free_msg(msg);	/* degenerate disposal case */
			caps_drop(rcaps);
		    } else {
			caps_put_msg(rcaps, msg, CAPMS_DISPOSE);
		    }
		    break;
		case CAPMS_DISPOSE:
		    caps_free_msg(msg);
		    break;
		}
	    }
	}
	while ((msg = TAILQ_FIRST(&tmpuserq)) != NULL) {
	    TAILQ_REMOVE(&tmpuserq, msg, km_node);
	    TAILQ_INSERT_TAIL(&caps->ci_msguserq, msg, km_node);
	    msg->km_flags |= CAPKMF_ONUSERQ;
	}
	while ((msg = TAILQ_FIRST(&tmppendq)) != NULL) {
	    TAILQ_REMOVE(&tmppendq, msg, km_node);
	    TAILQ_INSERT_TAIL(&caps->ci_msgpendq, msg, km_node);
	    msg->km_flags |= CAPKMF_ONPENDQ;
	}
    }
    if ((flags & CAPKF_HLIST) && (caps->ci_flags & CAPKF_HLIST)) {
	for (scan = caps_hash(caps->ci_name, caps->ci_namelen);
	    *scan != caps;
	    scan = &(*scan)->ci_hnext
	) {
	    KKASSERT(*scan != NULL);
	}
	*scan = caps->ci_hnext;
	caps->ci_hnext = (void *)-1;
	caps->ci_flags &= ~CAPKF_HLIST;
    }
    if ((flags & CAPKF_TDLIST) && (caps->ci_flags & CAPKF_TDLIST)) {
	for (scan = &caps->ci_td->td_caps;
	    *scan != caps;
	    scan = &(*scan)->ci_tdnext
	) {
	    KKASSERT(*scan != NULL);
	}
	*scan = caps->ci_tdnext;
	caps->ci_flags &= ~CAPKF_TDLIST;
	caps->ci_tdnext = (void *)-1;
	caps->ci_td = NULL;
	caps_drop(caps);
    }
    if ((flags & CAPKF_RCAPS) && (caps->ci_flags & CAPKF_RCAPS)) {
	caps_kinfo_t ctmp;

	caps->ci_flags &= ~CAPKF_RCAPS;
	if ((ctmp = caps->ci_rcaps)) {
	    caps->ci_rcaps = NULL;
	    caps_term(ctmp, CAPKF_FLUSH, caps);
	    caps_drop(ctmp);
	}
    }
}

static void
caps_free(caps_kinfo_t caps)
{
    KKASSERT(TAILQ_EMPTY(&caps->ci_msgpendq));
    KKASSERT(TAILQ_EMPTY(&caps->ci_msguserq));
    KKASSERT((caps->ci_flags & (CAPKF_HLIST|CAPKF_TDLIST)) == 0);
    kfree(caps, M_CAPS);
}

/************************************************************************
 *			PROCESS SUPPORT FUNCTIONS			*
 ************************************************************************/

/*
 * Create dummy entries in p2 so we can return the appropriate
 * error code.  Robust userland code will check the error for a
 * forked condition and reforge the connection.
 */
void
caps_fork(struct thread *td1, struct thread *td2)
{
    caps_kinfo_t caps1;
    caps_kinfo_t caps2;

    /*
     * Create dummy entries with the same id's as the originals.  Note
     * that service entries are not re-added to the hash table. The
     * dummy entries return an ENOTCONN error allowing userland code to
     * detect that a fork occured.  Userland must reconnect to the service.
     */
    for (caps1 = td1->td_caps; caps1; caps1 = caps1->ci_tdnext) {
	if (caps1->ci_flags & CAPF_NOFORK)
		continue;
	caps2 = caps_alloc(td2,
			caps1->ci_name, caps1->ci_namelen,
			caps1->ci_uid, caps1->ci_gid,
			caps1->ci_flags & CAPF_UFLAGS, CAPT_FORKED);
	caps2->ci_id = caps1->ci_id;
    }

    /*
     * Reverse the list order to maintain highest-id-first
     */
    caps2 = td2->td_caps;
    td2->td_caps = NULL;
    while (caps2) {
	caps1 = caps2->ci_tdnext;
	caps2->ci_tdnext = td2->td_caps;
	td2->td_caps = caps2;
	caps2 = caps1;
    }
}

void
caps_exit(struct thread *td)
{
    caps_kinfo_t caps;

    while ((caps = td->td_caps) != NULL) {
	caps_hold(caps);
	caps_term(caps, CAPKF_TDLIST|CAPKF_HLIST|CAPKF_FLUSH|CAPKF_RCAPS, NULL);
	caps_drop(caps);
    }
}

/************************************************************************
 *				SYSTEM CALLS				*
 ************************************************************************/

/*
 * caps_sys_service(name, uid, gid, upcid, flags);
 *
 * Create an IPC service using the specified name, uid, gid, and flags.
 * Either uid or gid can be -1, but not both.  The port identifier is
 * returned.
 *
 * upcid can either be an upcall or a kqueue identifier (XXX)
 *
 * MPALMOSTSAFE
 */
int
sys_caps_sys_service(struct caps_sys_service_args *uap)
{
    struct ucred *cred = curthread->td_ucred;
    char name[CAPS_MAXNAMELEN];
    caps_kinfo_t caps;
    size_t len;
    int error;

    if (caps_enabled == 0)
	return(EOPNOTSUPP);
    if ((error = copyinstr(uap->name, name, CAPS_MAXNAMELEN, &len)) != 0)
	return(error);
    if ((ssize_t)--len <= 0)
	return(EINVAL);
    get_mplock();

    if ((error = caps_name_check(name, len)) == 0) {
	caps = kern_caps_sys_service(name, uap->uid, uap->gid, cred,
				    uap->flags & CAPF_UFLAGS, &error);
	if (caps)
	    uap->sysmsg_result = caps->ci_id;
    }
    rel_mplock();
    return(error);
}

/*
 * caps_sys_client(name, uid, gid, upcid, flags);
 *
 * Create an IPC client connected to the specified service.  Either uid or gid
 * may be -1, indicating a wildcard, but not both.  The port identifier is
 * returned.
 *
 * upcid can either be an upcall or a kqueue identifier (XXX)
 *
 * MPALMOSTSAFE
 */
int
sys_caps_sys_client(struct caps_sys_client_args *uap)
{
    struct ucred *cred = curthread->td_ucred;
    char name[CAPS_MAXNAMELEN];
    caps_kinfo_t caps;
    size_t len;
    int error;

    if (caps_enabled == 0)
	return(EOPNOTSUPP);
    if ((error = copyinstr(uap->name, name, CAPS_MAXNAMELEN, &len)) != 0)
	return(error);
    if ((ssize_t)--len <= 0)
	return(EINVAL);
    get_mplock();

    if ((error = caps_name_check(name, len)) == 0) {
	caps = kern_caps_sys_client(name, uap->uid, uap->gid, cred,
				    uap->flags & CAPF_UFLAGS, &error);
	if (caps)
	    uap->sysmsg_result = caps->ci_id;
    }
    rel_mplock();
    return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_caps_sys_close(struct caps_sys_close_args *uap)
{
    struct thread *td = curthread;
    caps_kinfo_t caps;
    int error;

    get_mplock();

    if ((caps = caps_find_id(td, uap->portid)) != NULL) {
	    caps_term(caps, CAPKF_TDLIST|CAPKF_HLIST|CAPKF_FLUSH|CAPKF_RCAPS,
		      NULL);
	    caps_drop(caps);
	    error = 0;
    } else {
	    error = EINVAL;
    }
    rel_mplock();
    return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_caps_sys_setgen(struct caps_sys_setgen_args *uap)
{
    struct thread *td = curthread;
    caps_kinfo_t caps;
    int error;

    get_mplock();

    if ((caps = caps_find_id(td, uap->portid)) != NULL) {
	if (caps->ci_type == CAPT_FORKED) {
	    error = ENOTCONN;
	} else {
	    caps->ci_gen = uap->gen;
	    error = 0;
	}
	caps_drop(caps);
    } else {
	error = EINVAL;
    }
    rel_mplock();
    return(error);
}

/*
 * MPALMOSTSAFE
 */
int
sys_caps_sys_getgen(struct caps_sys_getgen_args *uap)
{
    struct thread *td = curthread;
    caps_kinfo_t caps;
    int error;

    get_mplock();

    if ((caps = caps_find_id(td, uap->portid)) != NULL) {
	if (caps->ci_type == CAPT_FORKED) {
	    error = ENOTCONN;
	} else if (caps->ci_rcaps == NULL) {
	    error = EINVAL;
	} else {
	    uap->sysmsg_result64 = caps->ci_rcaps->ci_gen;
	    error = 0;
	}
	caps_drop(caps);
    } else {
	error = EINVAL;
    }
    rel_mplock();
    return(error);
}

/*
 * caps_sys_put(portid, msg, msgsize)
 *
 * Send an opaque message of the specified size to the specified port.  This
 * function may only be used with a client port.  The message id is returned.
 *
 * MPALMOSTSAFE
 */
int
sys_caps_sys_put(struct caps_sys_put_args *uap)
{
    struct thread *td = curthread;
    caps_kinfo_t caps;
    caps_kmsg_t msg;
    int error;

    if (uap->msgsize < 0)
	return(EINVAL);
    get_mplock();

    if ((caps = caps_find_id(td, uap->portid)) == NULL) {
	error = EINVAL;
	goto done;
    }
    if (caps->ci_type == CAPT_FORKED) {
	error = ENOTCONN;
    } else if (caps->ci_rcaps == NULL) {
	error = EINVAL;
    } else if (caps->ci_cmsgcount > CAPS_MAXINPROG) {
	/*
	 * If this client has queued a large number of messages return
	 * ENOBUFS.  The client must process some replies before it can
	 * send new messages.  The server can also throttle a client by
	 * holding its replies.  XXX allow a server to refuse messages from
	 * a client.
	 */
	error = ENOBUFS;
    } else {
	msg = caps_alloc_msg(caps);
	uap->sysmsg_offset = msg->km_msgid.c_id;

	/*
	 * If the remote end is closed return ENOTCONN immediately, otherwise
	 * send it to the remote end.
	 *
	 * Note: since this is a new message, caps_load_ccr() returns a remote
	 * caps of NULL.
	 */
	if (caps->ci_rcaps->ci_flags & CAPKF_CLOSED) {
	    error = ENOTCONN;
	    caps_free_msg(msg);
	} else {
	    /*
	     * new message, load_ccr returns NULL. hold rcaps for put_msg
	     */
	    error = 0;
	    caps_load_ccr(caps, msg, td->td_lwp, uap->msg, uap->msgsize);
	    caps_hold(caps->ci_rcaps);
	    ++caps->ci_cmsgcount;
	    caps_put_msg(caps->ci_rcaps, msg, CAPMS_REQUEST); /* drops rcaps */
	}
    }
    caps_drop(caps);
done:
    rel_mplock();
    return(error);
}

/*
 * caps_sys_reply(portid, msg, msgsize, msgid)
 *
 * Reply to the message referenced by the specified msgid, supplying opaque
 * data back to the originator.
 *
 * MPALMOSTSAFE
 */
int
sys_caps_sys_reply(struct caps_sys_reply_args *uap)
{
    struct thread *td = curthread;
    caps_kinfo_t caps;
    caps_kinfo_t rcaps;
    caps_kmsg_t msg;
    int error;

    if (uap->msgsize < 0)
	return(EINVAL);
    get_mplock();

    if ((caps = caps_find_id(td, uap->portid)) == NULL) {
	error = EINVAL;
	goto done;
    }
    if (caps->ci_type == CAPT_FORKED) {
	/*
	 * The caps structure is just a fork placeholder, tell the caller
	 * that he has to reconnect.
	 */
	error = ENOTCONN;
    } else if ((msg = caps_find_msg(caps, uap->msgcid)) == NULL) {
	/*
	 * Could not find message being replied to (other side might have
	 * gone away).
	 */
	error = EINVAL;
    } else if ((msg->km_flags & CAPKMF_ONUSERQ) == 0) {
	/*
	 * Trying to reply to a non-replyable message
	 */
	error = EINVAL;
    } else {
	/*
	 * If the remote end is closed requeue to ourselves for disposal.
	 * Otherwise send the reply to the other end (the other end will
	 * return a passive DISPOSE to us when it has eaten the data)
	 */
	error = 0;
	caps_dequeue_msg(caps, msg);
	if (msg->km_mcaps->ci_flags & CAPKF_CLOSED) {
	    caps_drop(caps_load_ccr(caps, msg, td->td_lwp, NULL, 0));
	    caps_hold(caps);			/* ref for message */
	    caps_put_msg(caps, msg, CAPMS_DISPOSE);
	} else {
	    rcaps = caps_load_ccr(caps, msg, td->td_lwp, uap->msg, uap->msgsize);
	    caps_put_msg(rcaps, msg, CAPMS_REPLY);
	}
    }
    caps_drop(caps);
done:
    rel_mplock();
    return(error);
}

/*
 * caps_sys_get(portid, msg, maxsize, msgid, ccr)
 *
 * Retrieve the next ready message on the port, store its message id in
 * uap->msgid and return the length of the message.  If the message is too
 * large to fit the message id, length, and creds are still returned, but
 * the message is not dequeued (the caller is expected to call again with
 * a larger buffer or to reply the messageid if it does not want to handle
 * the message).
 *
 * EWOULDBLOCK is returned if no messages are pending.  Note that 0-length
 * messages are perfectly acceptable so 0 can be legitimately returned.
 *
 * MPALMOSTSAFE
 */
int
sys_caps_sys_get(struct caps_sys_get_args *uap)
{
    struct thread *td = curthread;
    caps_kinfo_t caps;
    caps_kmsg_t msg;
    int error;

    if (uap->maxsize < 0)
	return(EINVAL);
    get_mplock();

    if ((caps = caps_find_id(td, uap->portid)) != NULL) {
	if (caps->ci_type == CAPT_FORKED) {
	    error = ENOTCONN;
	} else if ((msg = TAILQ_FIRST(&caps->ci_msgpendq)) == NULL) {
	    error = EWOULDBLOCK;
	} else {
	    error = caps_process_msg(caps, msg, uap);
	}
	caps_drop(caps);
    } else {
	error = EINVAL;
    }
    rel_mplock();
    return(error);
}

/*
 * caps_sys_wait(portid, msg, maxsize, msgid, ccr)
 *
 * Retrieve the next ready message on the port, store its message id in
 * uap->msgid and return the length of the message.  If the message is too
 * large to fit the message id, length, and creds are still returned, but
 * the message is not dequeued (the caller is expected to call again with
 * a larger buffer or to reply the messageid if it does not want to handle
 * the message).
 *
 * This function blocks until interrupted or a message is received.
 * Note that 0-length messages are perfectly acceptable so 0 can be
 * legitimately returned.
 *
 * MPALMOSTSAFE
 */
int
sys_caps_sys_wait(struct caps_sys_wait_args *uap)
{
    struct thread *td = curthread;
    caps_kinfo_t caps;
    caps_kmsg_t msg;
    int error;

    if (uap->maxsize < 0)
	return(EINVAL);
    get_mplock();

    if ((caps = caps_find_id(td, uap->portid)) != NULL) {
	if (caps->ci_type == CAPT_FORKED) {
	    error = ENOTCONN;
	} else {
	    error = 0;
	    while ((msg = TAILQ_FIRST(&caps->ci_msgpendq)) == NULL) {
		if ((error = tsleep(caps, PCATCH, "caps", 0)) != 0)
		    break;
	    }
	    if (error == 0) {
		error = caps_process_msg(caps, msg,
				    (struct caps_sys_get_args *)uap);
	    }
	}
	caps_drop(caps);
    } else {
	error = EINVAL;
    }
    rel_mplock();
    return(error);
}

static int
caps_process_msg(caps_kinfo_t caps, caps_kmsg_t msg,
		 struct caps_sys_get_args *uap)
{
    struct thread *td = curthread;
    int error = 0;
    int msgsize;
    caps_kinfo_t rcaps;

    msg->km_flags |= CAPKMF_PEEKED;
    msgsize = msg->km_xio.xio_bytes;
    if (msgsize <= uap->maxsize)
	caps_dequeue_msg(caps, msg);

    if (msg->km_xio.xio_bytes != 0) {
	error = xio_copy_xtou(&msg->km_xio, 0, uap->msg, 
			    min(msg->km_xio.xio_bytes, uap->maxsize));
	if (error) {
	    if (msg->km_mcaps->ci_td && msg->km_mcaps->ci_td->td_proc) {
		kprintf("xio_copy_xtou: error %d from proc %d\n", 
			error, msg->km_mcaps->ci_td->td_proc->p_pid);
	    }
	    if (msgsize > uap->maxsize)
		caps_dequeue_msg(caps, msg);
	    msgsize = 0;
	    error = 0;
	}
    }

    if (uap->msgid)
	error = copyout(&msg->km_msgid, uap->msgid, sizeof(msg->km_msgid));
    if (uap->ccr)
	error = copyout(&msg->km_ccr, uap->ccr, sizeof(msg->km_ccr));
    if (error == 0)
	uap->sysmsg_result = msgsize;

    /*
     * If the message was dequeued we must deal with it.
     */
    if (msgsize <= uap->maxsize) {
	switch(msg->km_state) {
	case CAPMS_REQUEST:
	case CAPMS_REQUEST_RETRY:
	    TAILQ_INSERT_TAIL(&caps->ci_msguserq, msg, km_node);
	    msg->km_flags |= CAPKMF_ONUSERQ;
	    break;
	case CAPMS_REPLY:
	case CAPMS_REPLY_RETRY:
	    --caps->ci_cmsgcount;
	    rcaps = caps_load_ccr(caps, msg, td->td_lwp, NULL, 0);
	    if (caps == rcaps || (rcaps->ci_flags & CAPKF_CLOSED)) {
		/* degenerate disposal case */
		caps_free_msg(msg);
		caps_drop(rcaps);
	    } else {
		caps_put_msg(rcaps, msg, CAPMS_DISPOSE);
	    }
	    break;
	case CAPMS_DISPOSE:
	    caps_free_msg(msg);
	    break;
	}
    }
    return(error);
}

/*
 * caps_sys_abort(portid, msgcid, flags)
 *
 *	Abort a previously sent message.  You must still wait for the message
 *	to be returned after sending the abort request.  This function will
 *	return the appropriate CAPS_ABORT_* code depending on what it had
 *	to do.
 *
 * MPALMOSTSAFE
 */
int
sys_caps_sys_abort(struct caps_sys_abort_args *uap)
{
    uap->sysmsg_result = CAPS_ABORT_NOTIMPL;
    return(0);
}

/*
 * KERNEL SYSCALL SEPARATION SUPPORT FUNCTIONS
 */

static
caps_kinfo_t
kern_caps_sys_service(const char *name, uid_t uid, gid_t gid,
			struct ucred *cred, int flags, int *error)
{
    struct thread *td = curthread;
    caps_kinfo_t caps;
    int len;

    len = strlen(name);

    /*
     * Make sure we can use the uid and gid
     */
    if (cred) {
	if (cred->cr_uid != 0 && uid != (uid_t)-1 && cred->cr_uid != uid) {
	    *error = EPERM;
	    return(NULL);
	}
	if (cred->cr_uid != 0 && gid != (gid_t)-1 && !groupmember(gid, cred)) {
	    *error = EPERM;
	    return(NULL);
	}
    }

    /*
     * Handle CAPF_EXCL
     */
    if (flags & CAPF_EXCL) {
	if ((caps = caps_find(name, strlen(name), uid, gid)) != NULL) {
	    caps_drop(caps);
	    *error = EEXIST;
	    return(NULL);
	}
    }

    /*
     * Create the service
     */
    caps = caps_alloc(td, name, len,
			uid, gid, flags & CAPF_UFLAGS, CAPT_SERVICE);
    wakeup(&caps_waitsvc);
    return(caps);
}

static
caps_kinfo_t
kern_caps_sys_client(const char *name, uid_t uid, gid_t gid,
			struct ucred *cred, int flags, int *error)
{
    struct thread *td = curthread;
    caps_kinfo_t caps, rcaps;
    int len;

    len = strlen(name);

    /*
     * Locate the CAPS service (rcaps ref is for caps->ci_rcaps)
     */
again:
    if ((rcaps = caps_find(name, len, uid, gid)) == NULL) {
	if (flags & CAPF_WAITSVC) {
	    char cbuf[32];
	    ksnprintf(cbuf, sizeof(cbuf), "C%s", name);
	    *error = tsleep(&caps_waitsvc, PCATCH, cbuf, 0);
	    if (*error == 0)
		goto again;
	} else {
	    *error = ENOENT;
	}
	return(NULL);
    }

    /*
     * Check permissions
     */
    if (cred) {
	*error = EACCES;
	if ((flags & CAPF_USER) && (rcaps->ci_flags & CAPF_USER)) {
	    if (rcaps->ci_uid != (uid_t)-1 && rcaps->ci_uid == cred->cr_uid)
		*error = 0;
	}
	if ((flags & CAPF_GROUP) && (rcaps->ci_flags & CAPF_GROUP)) {
	    if (rcaps->ci_gid != (gid_t)-1 && groupmember(rcaps->ci_gid, cred))
		*error = 0;
	}
	if ((flags & CAPF_WORLD) && (rcaps->ci_flags & CAPF_WORLD)) {
	    *error = 0;
	}
	if (*error) {
	    caps_drop(rcaps);
	    return(NULL);
	}
    } else {
	*error = 0;
    }

    /*
     * Allocate the client side and connect to the server
     */
    caps = caps_alloc(td, name, len,
			uid, gid, flags & CAPF_UFLAGS, CAPT_CLIENT);
    caps->ci_rcaps = rcaps;
    caps->ci_flags |= CAPKF_RCAPS;
    return(caps);
}

