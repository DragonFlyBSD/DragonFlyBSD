/*
 * CAPS_MISC.C
 *
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
 * $DragonFly: src/lib/libcaps/Attic/caps_misc.c,v 1.1 2003/11/24 21:15:58 dillon Exp $
 */

#include "defs.h"

caps_port_t
caps_mkport(
    enum caps_type type,
    int (*cs_putport)(lwkt_port_t port, lwkt_msg_t msg),
    void *(*cs_waitport)(lwkt_port_t port, lwkt_msg_t msg),
    void (*cs_replyport)(lwkt_port_t port, lwkt_msg_t msg)
) {
    caps_port_t port;

    port = malloc(sizeof(*port));
    bzero(port, sizeof(*port));

    lwkt_initport(&port->lport, curthread);
    port->lport.mp_putport = cs_putport;
    port->lport.mp_waitport = cs_waitport;
    port->lport.mp_replyport = cs_replyport;
    port->lport.mp_refs = 1;
    port->type = type;
    port->kqfd = -1;		/* kqueue descriptor */
    port->lfd = -1;		/* listen socket descriptor */
    port->cfd = -1;		/* client socket descriptor */
    TAILQ_INIT(&port->clist);	/* server connections */
    TAILQ_INIT(&port->wlist);	/* writes in progress */
    TAILQ_INIT(&port->mlist);	/* written messages waiting for reply */
    port->cred.pid = (pid_t)-1;
    port->cred.uid = (uid_t)-1;
    port->cred.gid = (gid_t)-1;

    port->rmsg = &port->rmsg_static;

    return(port);
}

void
caps_shutdown(caps_port_t port)
{
    caps_port_t scan;
    lwkt_msg_t msg;

    port->flags |= CAPPF_SHUTDOWN;
    if (port->flags & CAPPF_ONLIST) {
	--port->lport.mp_refs;
	port->flags &= ~CAPPF_ONLIST;
	TAILQ_REMOVE(&port->server->clist, port, centry);
    }
    if (port->kqfd >= 0) {
	close(port->kqfd);
	port->kqfd = -1;
    }
    if (port->lfd >= 0) {
	close(port->lfd);
	port->lfd = -1;
    }
    if (port->cfd >= 0) {
	close(port->cfd);
	port->cfd = -1;
    }
    port->rbytes = 0;
    port->wbytes = 0;
    while ((msg = TAILQ_FIRST(&port->wlist)) != NULL) {
	TAILQ_REMOVE(&port->wlist, msg, ms_node);
	msg->ms_flags &= ~MSGF_QUEUED;
	if (port->type == CAPT_CLIENT)
	    lwkt_replymsg(msg, EIO);
	else
	    free(msg);
    }
    while ((msg = TAILQ_FIRST(&port->mlist)) != NULL) {
	TAILQ_REMOVE(&port->mlist, msg, ms_node);
	msg->ms_flags &= ~MSGF_QUEUED;
	lwkt_replymsg(msg, EIO);
    }
    if ((msg = port->rmsg) != NULL) {
	port->rmsg = &port->rmsg_static;
	if (msg != &port->rmsg_static)
	    free(msg);
    }
    while ((scan = TAILQ_FIRST(&port->clist)) != NULL) {
	caps_shutdown(scan);
    }
    assert(port->lport.mp_refs >= 0);
    if (port->lport.mp_refs == 0)
	free(port);
}

void
caps_close(caps_port_t port)
{
    --port->lport.mp_refs;
    assert(port->lport.mp_refs >= 0);
    caps_shutdown(port);
}

/*
 * Start writing a new message to the socket and/or continue writing
 * previously queued messages to the socket.
 */
void
caps_kev_write(caps_port_t port, lwkt_msg_t msg)
{
    struct kevent kev;
    int n;

    /*
     * Add new messages to the queue
     */
    if (msg) {
	msg->ms_flags |= MSGF_QUEUED;
	TAILQ_INSERT_TAIL(&port->wlist, msg, ms_node);
    }

    /*
     * Continue writing out the existing queue.  The message in
     * progress is msg->ms_msgsize bytes long.  The opaque field in
     * the over-the-wire version of the message contains a pointer
     * to the message so we can match up replies.
     */
    ++port->lport.mp_refs;
    while ((msg = TAILQ_FIRST(&port->wlist)) != NULL) {
	lwkt_msg_t save;

	/*
	 * Kinda messy
	 */
	save = msg->opaque.ms_umsg;
	if ((msg->ms_flags & MSGF_REPLY) == 0)
	    msg->opaque.ms_umsg = msg;
	n = write(port->cfd, (char *)msg + port->wbytes, 
		    msg->ms_msgsize - port->wbytes);
	msg->opaque.ms_umsg = save;

	DBPRINTF(("write %d/%d bytes\n" , n, msg->ms_msgsize - port->wbytes));
	/* XXX handle failures.  Let the read side deal with it */
	if (n <= 0)
	    break;
	port->wbytes += n;
	if (port->wbytes != msg->ms_msgsize)
	    break;
	port->wbytes = 0;
	TAILQ_REMOVE(&port->wlist, msg, ms_node);
	msg->ms_flags &= ~MSGF_QUEUED;
	if (msg->ms_flags & MSGF_REPLY) {
	    /*
	     * Finished writing reply, throw the message away.
	     */
	    free(msg);
	} else {
	    /*
	     * Finished sending request, place message on mlist.
	     */
	    msg->ms_flags |= MSGF_QUEUED;
	    TAILQ_INSERT_TAIL(&port->mlist, msg, ms_node);
	}
    }

    /*
     * Do we need to wait for a write-availability event?   Note that
     * the kevent calls can fail if the descriptor is no longer valid.
     */
    msg = TAILQ_FIRST(&port->wlist);
    if (msg && (port->flags & CAPPF_WREQUESTED) == 0) {
	port->flags |= CAPPF_WREQUESTED;
	EV_SET(&kev, port->cfd, EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, port);
	kevent(port->kqfd, &kev, 1, NULL, 0, NULL);
    } else if (port->flags & CAPPF_WREQUESTED) {
	port->flags &= ~CAPPF_WREQUESTED;
	EV_SET(&kev, port->cfd, EVFILT_WRITE, EV_ADD|EV_DISABLE, 0, 0, port);
	kevent(port->kqfd, &kev, 1, NULL, 0, NULL);
    }
    --port->lport.mp_refs;
}

/*
 * Read a new message from the socket or continue reading messages from the
 * socket.  If the message represents a reply it must be matched up against
 * messages on the mlist, copied, and the mlist message returned instead.
 */
lwkt_msg_t
caps_kev_read(caps_port_t port)
{
    lwkt_msg_t msg;
    int n;

    /*
     * If we are waiting for a cred the only permissable message is a
     * creds message.
     */
    if (port->flags & CAPPF_WAITCRED) {
	struct msghdr msghdr;
	struct caps_creds_cmsg cmsg;

	bzero(&msghdr, sizeof(msghdr));
	bzero(&cmsg, sizeof(cmsg));
	msghdr.msg_control = &cmsg;
	msghdr.msg_controllen = sizeof(cmsg);
	cmsg.cmsg.cmsg_len = sizeof(cmsg);
	cmsg.cmsg.cmsg_type = 0;
	if ((n = recvmsg(port->cfd, &msghdr, MSG_EOR)) < 0) {
	    if (errno == EINTR)
		return(NULL);
	}
	if (cmsg.cmsg.cmsg_type != SCM_CREDS) {
	    DBPRINTF(("server: expected SCM_CREDS\n"));
	    goto failed;
	}
	DBPRINTF(("server: connect from pid %d uid %d\n",
		(int)cmsg.cred.cmcred_pid, (int)cmsg.cred.cmcred_uid));
	port->cred.pid = cmsg.cred.cmcred_pid;
	port->cred.uid = cmsg.cred.cmcred_uid;
	port->cred.euid = cmsg.cred.cmcred_euid;
	port->cred.gid = cmsg.cred.cmcred_gid;
	if ((port->cred.ngroups = cmsg.cred.cmcred_ngroups) > CAPS_MAXGROUPS)
	    port->cred.ngroups = CAPS_MAXGROUPS;
	if (port->cred.ngroups < 0)
	    port->cred.ngroups = 0;
	bcopy(cmsg.cred.cmcred_groups, port->cred.groups, 
		sizeof(gid_t) * port->cred.ngroups);
	port->flags &= ~CAPPF_WAITCRED;
	return(NULL);
    }

    /*
     * Read or continue reading the next packet.  Use the static message
     * while we are pulling in the header.
     */
    if (port->rmsg == &port->rmsg_static) {
	n = read(port->cfd, (char *)port->rmsg + port->rbytes,
		sizeof(port->rmsg_static) - port->rbytes);
	DBPRINTF(("read %d bytes\n" , n));
	if (n <= 0) {
		if (errno == EINTR || errno == EAGAIN)
		    return(NULL);
		goto failed;
	}
	port->rbytes += n;
	if (port->rbytes != sizeof(port->rmsg_static))
	    return(NULL);
	if (port->rmsg_static.ms_msgsize > port->rmsg_static.ms_maxsize ||
	    port->rmsg_static.ms_msgsize < sizeof(struct lwkt_msg) ||
	    port->rmsg_static.ms_maxsize > CAPMSG_MAXSIZE
	) {
	    goto failed;
	}
	port->rmsg = malloc(port->rmsg_static.ms_maxsize);
	bcopy(&port->rmsg_static, port->rmsg, port->rbytes);
    }
    if (port->rbytes != port->rmsg->ms_msgsize) {
	n = read(port->cfd, (char *)port->rmsg + port->rbytes,
		port->rmsg->ms_msgsize - port->rbytes);
	if (n <= 0) {
	    if (errno == EINTR || errno == EAGAIN)
		return(NULL);
	    goto failed;
	}
	port->rbytes += n;
	if (port->rbytes != port->rmsg->ms_msgsize)
	    return(NULL);
    }
    msg = port->rmsg;
    port->rmsg = &port->rmsg_static;
    port->rbytes = 0;

    /*
     * Setup the target port and the reply port
     */
    msg->ms_reply_port = &port->lport;
    if (port->type == CAPT_REMOTE && port->server)
	msg->ms_target_port = &port->server->lport;
    else
	msg->ms_target_port = &port->lport;

    if (msg->ms_flags & MSGF_REPLY) {
	/*
	 * If the message represents a reply we have to match it up against
	 * the original.
	 */
	lwkt_msg_t scan;
	lwkt_msg_t save_msg;
	lwkt_port_t save_port1;
	lwkt_port_t save_port2;

	TAILQ_FOREACH(scan, &port->mlist, ms_node) {
	    if (msg->opaque.ms_umsg == scan)
		break;
	}
	DBPRINTF(("matchup: %p against %p\n", msg->opaque.ms_umsg, scan));
	if (scan == NULL)
	    goto failed;
	if (msg->ms_msgsize > scan->ms_maxsize)
	    goto failed;
	TAILQ_REMOVE(&port->mlist, scan, ms_node);
	save_msg = scan->opaque.ms_umsg;
	save_port1 = scan->ms_target_port;
	save_port2 = scan->ms_reply_port;
	bcopy(msg, scan, msg->ms_msgsize);
	scan->opaque.ms_umsg = save_msg;
	scan->ms_target_port = save_port1;
	scan->ms_reply_port = save_port2;
	free(msg);
	msg = scan;
    } else {
	/*
	 * New messages ref the port so it cannot go away until the last
	 * message has been replied.
	 */
	++port->lport.mp_refs;
    }
    return(msg);
failed:
    caps_shutdown(port);
    return(NULL);
}

