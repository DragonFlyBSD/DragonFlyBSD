/*
 * Copyright (c) 2003 Matthew Dillon <dillon@backplane.com>
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
 * $DragonFly: src/sys/kern/lwkt_caps.c,v 1.2 2004/03/06 22:14:09 dillon Exp $
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
#include <vm/vm.h>
#include <vm/vm_extern.h>

static int caps_process_msg(caps_kinfo_t caps, caps_kmsg_t msg, struct caps_sys_get_args *uap);
static void caps_free(caps_kinfo_t caps);
static void caps_free_msg(caps_kmsg_t msg);
static int caps_name_check(const char *name, int len);
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

    caps = malloc(offsetof(struct caps_kinfo, ci_name[len+1]), 
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

    msg = malloc(sizeof(struct caps_kmsg), M_CAPS, M_WAITOK|M_ZERO);
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
caps_load_ccr(caps_kinfo_t caps, caps_kmsg_t msg, struct proc *p, void *udata, int ubytes)
{
    int i;
    struct ucred *cr = p->p_ucred;
    caps_kinfo_t rcaps;

    /*
     * replace km_mcaps with new VM state, return the old km_mcaps.  We
     * dereference the old mcap's mrefs but do not drop its main ref count.
     * The caller is expected to do that.
     */
    rcaps = caps_free_msg_mcaps(msg);	/* can be NULL */
    ++caps->ci_mrefs;
    caps_hold(caps);
    msg->km_mcaps = caps;
    msg->km_umsg = udata;
    msg->km_umsg_size = ubytes;

    msg->km_ccr.pid = p ? p->p_pid : -1;
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
 *
 * Free the vmspace reference relating to the data associated with the
 * message (this prevents the target process from exiting too early).
 * Return and clear km_mcaps.  The caller is responsible for dropping the
 * reference to the returned caps.
 */
static
caps_kinfo_t
caps_free_msg_mcaps(caps_kmsg_t msg)
{
    caps_kinfo_t mcaps;

    if ((mcaps = msg->km_mcaps) != NULL) {
	msg->km_mcaps = NULL;
	if (--mcaps->ci_mrefs == 0 && (mcaps->ci_flags & CAPKF_MWAIT))
	    wakeup(mcaps);
    }
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
    free(msg, M_CAPS);
}

/*
 * Validate the service name
 */
static int
caps_name_check(const char *name, int len)
{
    int i;
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
		    rcaps = caps_load_ccr(caps, msg, curproc, NULL, 0);
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
			    caps_hold(caps);
			    caps_put_msg(caps, msg, CAPMS_DISPOSE);
			}
		    } else {
			/*
			 * auto-reply to the originator.
			 */
			caps_put_msg(rcaps, msg, CAPMS_REPLY);
		    }
		    break;
		case CAPMS_REPLY:
		case CAPMS_REPLY_RETRY:
		    rcaps = caps_load_ccr(caps, msg, curproc, NULL, 0);
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
	while (caps->ci_mrefs) {
	    caps->ci_flags |= CAPKF_MWAIT;
	    tsleep(caps, 0, "cexit", 0);
	}
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
    free(caps, M_CAPS);
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
caps_fork(struct proc *p1, struct proc *p2, int flags)
{
    caps_kinfo_t caps1;
    caps_kinfo_t caps2;
    thread_t td1;
    thread_t td2;

    td1 = p1->p_thread;
    td2 = p2->p_thread;

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
 */
int
caps_sys_service(struct caps_sys_service_args *uap)
{
    struct ucred *cred = curproc->p_ucred;
    char name[CAPS_MAXNAMELEN];
    caps_kinfo_t caps;
    int len;
    int error;

    if (caps_enabled == 0)
	return(EOPNOTSUPP);
    if ((error = copyinstr(uap->name, name, CAPS_MAXNAMELEN, &len)) != 0)
	return(error);
    if (--len <= 0)
	return(EINVAL);
    if ((error = caps_name_check(name, len)) != 0)
	return(error);

    caps = kern_caps_sys_service(name, uap->uid, uap->gid, cred,
				uap->flags & CAPF_UFLAGS, &error);
    if (caps)
	uap->sysmsg_result = caps->ci_id;
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
 */
int
caps_sys_client(struct caps_sys_client_args *uap)
{
    struct ucred *cred = curproc->p_ucred;
    char name[CAPS_MAXNAMELEN];
    caps_kinfo_t caps;
    int len;
    int error;

    if (caps_enabled == 0)
	return(EOPNOTSUPP);
    if ((error = copyinstr(uap->name, name, CAPS_MAXNAMELEN, &len)) != 0)
	return(error);
    if (--len <= 0)
	return(EINVAL);
    if ((error = caps_name_check(name, len)) != 0)
	return(error);

    caps = kern_caps_sys_client(name, uap->uid, uap->gid, cred,
				uap->flags & CAPF_UFLAGS, &error);
    if (caps)
	uap->sysmsg_result = caps->ci_id;
    return(error);
}

int
caps_sys_close(struct caps_sys_close_args *uap)
{
    caps_kinfo_t caps;

    if ((caps = caps_find_id(curthread, uap->portid)) == NULL)
	return(EINVAL);
    caps_term(caps, CAPKF_TDLIST|CAPKF_HLIST|CAPKF_FLUSH|CAPKF_RCAPS, NULL);
    caps_drop(caps);
    return(0);
}

int
caps_sys_setgen(struct caps_sys_setgen_args *uap)
{
    caps_kinfo_t caps;

    if ((caps = caps_find_id(curthread, uap->portid)) == NULL)
	return(EINVAL);
    if (caps->ci_type == CAPT_FORKED)
	return(ENOTCONN);
    caps->ci_gen = uap->gen;
    return(0);
}

int
caps_sys_getgen(struct caps_sys_getgen_args *uap)
{
    caps_kinfo_t caps;

    if ((caps = caps_find_id(curthread, uap->portid)) == NULL)
	return(EINVAL);
    if (caps->ci_type == CAPT_FORKED)
	return(ENOTCONN);
    if (caps->ci_rcaps == NULL)
	return(EINVAL);
    uap->sysmsg_result64 = caps->ci_rcaps->ci_gen;
    return(0);
}

/*
 * caps_sys_put(portid, msg, msgsize)
 *
 * Send an opaque message of the specified size to the specified port.  This
 * function may only be used with a client port.  The message id is returned.
 */
int
caps_sys_put(struct caps_sys_put_args *uap)
{
    caps_kinfo_t caps;
    caps_kmsg_t msg;
    struct proc *p = curproc;
    int error;

    if (uap->msgsize < 0)
	return(EINVAL);
    if ((caps = caps_find_id(curthread, uap->portid)) == NULL)
	return(EINVAL);
    if (caps->ci_type == CAPT_FORKED)
	return(ENOTCONN);
    if (caps->ci_rcaps == NULL) {
	caps_drop(caps);
	return(EINVAL);
    }

    /*
     * If this client has queued a large number of messages return
     * ENOBUFS.  The client must process some replies before it can
     * send new messages.  The server can also throttle a client by
     * holding its replies.  XXX allow a server to refuse messages from
     * a client.
     */
    if (caps->ci_cmsgcount > CAPS_MAXINPROG) {
	caps_drop(caps);
	return(ENOBUFS);
    }
    msg = caps_alloc_msg(caps);
    uap->sysmsg_offset = msg->km_msgid.c_id;

    /*
     * If the remote end is closed return ENOTCONN immediately, otherwise
     * send it to the remote end.
     *
     * Note: since this is a new message, caps_load_ccr() returns a remote
     * caps of NULL.
     */
    error = 0;
    if (caps->ci_rcaps->ci_flags & CAPKF_CLOSED) {
	error = ENOTCONN;
	caps_free_msg(msg);
#if 0
	caps_load_ccr(caps, msg, p, NULL, 0);	/* returns NULL */
	caps_hold(caps);
	caps_put_msg(caps, msg, CAPMS_REPLY);	/* drops caps */
#endif
    } else {
	caps_load_ccr(caps, msg, p, uap->msg, uap->msgsize); /* returns NULL */
	caps_hold(caps->ci_rcaps);			  /* need ref */
	++caps->ci_cmsgcount;
	caps_put_msg(caps->ci_rcaps, msg, CAPMS_REQUEST); /* drops rcaps */
    }
    caps_drop(caps);
    return(error);
}

/*
 * caps_sys_reply(portid, msg, msgsize, msgid)
 *
 * Reply to the message referenced by the specified msgid, supplying opaque
 * data back to the originator.
 */
int
caps_sys_reply(struct caps_sys_reply_args *uap)
{
    caps_kinfo_t caps;
    caps_kinfo_t rcaps;
    caps_kmsg_t msg;
    struct proc *p;

    if (uap->msgsize < 0)
	return(EINVAL);
    if ((caps = caps_find_id(curthread, uap->portid)) == NULL)
	return(EINVAL);
    if (caps->ci_type == CAPT_FORKED)
	return(ENOTCONN);

    /*
     * Can't find message to reply to
     */
    if ((msg = caps_find_msg(caps, uap->msgcid)) == NULL) {
	caps_drop(caps);
	return(EINVAL);
    }

    /*
     * Trying to reply to a non-replyable message
     */
    if ((msg->km_flags & CAPKMF_ONUSERQ) == 0) {
	caps_drop(caps);
	return(EINVAL);
    }

    /*
     * If the remote end is closed requeue to ourselves for disposal.
     * Otherwise send the reply to the other end (the other end will
     * return a passive DISPOSE to us when it has eaten the data)
     */
    caps_dequeue_msg(caps, msg);
    p = curproc;
    if (msg->km_mcaps->ci_flags & CAPKF_CLOSED) {
	caps_drop(caps_load_ccr(caps, msg, p, NULL, 0));
	caps_hold(caps);
	caps_put_msg(caps, msg, CAPMS_DISPOSE);	/* drops caps */
    } else {
	rcaps = caps_load_ccr(caps, msg, p, uap->msg, uap->msgsize);
	caps_put_msg(rcaps, msg, CAPMS_REPLY);
    }
    caps_drop(caps);
    return(0);
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
 */
int
caps_sys_get(struct caps_sys_get_args *uap)
{
    caps_kinfo_t caps;
    caps_kmsg_t msg;

    if (uap->maxsize < 0)
	return(EINVAL);
    if ((caps = caps_find_id(curthread, uap->portid)) == NULL)
	return(EINVAL);
    if (caps->ci_type == CAPT_FORKED)
	return(ENOTCONN);
    if ((msg = TAILQ_FIRST(&caps->ci_msgpendq)) == NULL) {
	caps_drop(caps);
	return(EWOULDBLOCK);
    }
    return(caps_process_msg(caps, msg, uap));
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
 */
int
caps_sys_wait(struct caps_sys_wait_args *uap)
{
    caps_kinfo_t caps;
    caps_kmsg_t msg;
    int error;

    if (uap->maxsize < 0)
	return(EINVAL);
    if ((caps = caps_find_id(curthread, uap->portid)) == NULL)
	return(EINVAL);
    if (caps->ci_type == CAPT_FORKED)
	return(ENOTCONN);
    while ((msg = TAILQ_FIRST(&caps->ci_msgpendq)) == NULL) {
	if ((error = tsleep(caps, PCATCH, "caps", 0)) != 0) {
	    caps_drop(caps);
	    return(error);
	}
    }
    return(caps_process_msg(caps, msg, (struct caps_sys_get_args *)uap));
}

static int
caps_process_msg(caps_kinfo_t caps, caps_kmsg_t msg, struct caps_sys_get_args *uap)
{
    int error = 0;
    int msgsize;
    caps_kinfo_t rcaps;

    msg->km_flags |= CAPKMF_PEEKED;
    msgsize = msg->km_umsg_size;
    if (msgsize <= uap->maxsize)
	caps_dequeue_msg(caps, msg);

    if (msg->km_umsg_size != 0) {
	struct proc *rp = msg->km_mcaps->ci_td->td_proc;
	KKASSERT(rp != NULL);
	error = vmspace_copy(rp->p_vmspace, (vm_offset_t)msg->km_umsg,
			    curproc->p_vmspace, (vm_offset_t)uap->msg, 
			    min(msgsize, uap->maxsize), uap->maxsize);
	if (error) {
	    printf("vmspace_copy: error %d from proc %d\n", error, rp->p_pid);
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
	    rcaps = caps_load_ccr(caps, msg, curproc, NULL, 0);
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
    caps_drop(caps);
    return(error);
}

/*
 * caps_sys_abort(portid, msgcid, flags)
 *
 *	Abort a previously sent message.  You must still wait for the message
 *	to be returned after sending the abort request.  This function will
 *	return the appropriate CAPS_ABORT_* code depending on what it had
 *	to do.
 */
int
caps_sys_abort(struct caps_sys_abort_args *uap)
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
    caps = caps_alloc(curthread, name, len, 
			uid, gid, flags & CAPF_UFLAGS, CAPT_SERVICE);
    wakeup(&caps_waitsvc);
    return(caps);
}

static
caps_kinfo_t
kern_caps_sys_client(const char *name, uid_t uid, gid_t gid,
			struct ucred *cred, int flags, int *error)
{
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
	    snprintf(cbuf, sizeof(cbuf), "C%s", name);
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
    caps = caps_alloc(curthread, name, len, 
			uid, gid, flags & CAPF_UFLAGS, CAPT_CLIENT);
    caps->ci_rcaps = rcaps;
    caps->ci_flags |= CAPKF_RCAPS;
    return(caps);
}

