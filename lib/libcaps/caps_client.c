/*
 * CAPS_CLIENT.C
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
 * $DragonFly: src/lib/libcaps/Attic/caps_client.c,v 1.1 2003/11/24 21:15:58 dillon Exp $
 */
#include "defs.h"

static lwkt_msg_t caps_client_kev(struct kevent *kev);

static int caps_client_putport(lwkt_port_t port, lwkt_msg_t msg);
static void *caps_client_waitport(lwkt_port_t port, lwkt_msg_t msg);
static void caps_client_replyport(lwkt_port_t port, lwkt_msg_t msg);

/*
 * Connect to a remote service message port.
 *
 * Use the standard lwkt_*() messaging functions to send messages and wait
 * for replies.
 *
 * This code will eventually be replaced by a kernel API
 */
caps_port_t
caps_client(const char *name, uid_t uid, int flags)
{
    struct sockaddr_un sunix;
    struct kevent kev;
    caps_port_t port;
    int len;
    int error;

    /*
     * If uid is -1 first try our current uid, then try uid 0
     */
    if (uid == (uid_t)-1) {
	uid = getuid();
	if ((port = caps_client(name, uid, flags)) != NULL)
	    return(port);
	uid = 0;
    }

    /*
     * Allocate a port to handle incoming messages
     */
    port = caps_mkport(CAPT_CLIENT, 
			caps_client_putport,
			caps_client_waitport,
			caps_client_replyport);

    /*
     * Construct the unix domain socket.  If uid is -1 attempt to connect
     * to the services UID for this process (use VARSYM variable?). XXX
     */

    if ((port->cfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
	goto failed;
    bzero(&sunix, sizeof(sunix));
    sunix.sun_family = AF_UNIX;
    if (uid == 0) {
	snprintf(sunix.sun_path, sizeof(sunix.sun_path), 
		    CAPS_PATH1, name);
    } else {
	snprintf(sunix.sun_path, sizeof(sunix.sun_path),
		    CAPS_PATH2, (int)uid, name);
    }
    len = strlen(sunix.sun_path);

    error = connect(port->cfd, (void *)&sunix,
		    offsetof(struct sockaddr_un, sun_path[len+1]));
    if (error)
	goto failed;

    if ((port->kqfd = kqueue()) < 0)
	goto failed;

    /*
     * The server expects a cred immediately.
     */
    {
	struct msghdr msghdr;
	struct caps_creds_cmsg cmsg;

	bzero(&msghdr, sizeof(msghdr));
	bzero(&cmsg, sizeof(cmsg));
	msghdr.msg_control = &cmsg;
	msghdr.msg_controllen = sizeof(cmsg);
	cmsg.cmsg.cmsg_len = sizeof(cmsg);
	cmsg.cmsg.cmsg_level = SOL_SOCKET;
	cmsg.cmsg.cmsg_type = SCM_CREDS;
	if (sendmsg(port->cfd, &msghdr, 0) < 0) {
	    perror("sendmsg");
	    goto failed;
	}
    }
    fcntl(port->cfd, F_SETFL, O_NONBLOCK);
    EV_SET(&kev, port->cfd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, port);
    if (kevent(port->kqfd, &kev, 1, NULL, 0, NULL) < 0)
	goto failed;
    DBPRINTF(("Successfully created port %s\n", sunix.sun_path));
    return(port);
failed:
    caps_close(port);
    return(NULL);
}

/*
 * Service kqueue events on a client port.
 */
static lwkt_msg_t
caps_client_kev(struct kevent *kev)
{
    caps_port_t port = kev->udata;
    lwkt_msg_t msg;

    if (port->type == CAPT_CLIENT) {
	if (kev->filter == EVFILT_WRITE)
		caps_kev_write(port, NULL);
	if (kev->filter == EVFILT_READ) {
		msg = caps_kev_read(port);
	} else {
		msg = NULL;
	}
    } else {
	assert(0);
	msg = NULL;
    }
    return(msg);
}

/*
 * You can only reply to messages received on a services port.  You cannot
 * send a message to a services port (where would we send it?)
 */
static
int
caps_client_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    msg->ms_flags &= ~(MSGF_DONE | MSGF_REPLY);
    caps_kev_write((caps_port_t)port, msg);
    return(EASYNC);
}

/*
 * Temporary hack until LWKT threading is integrated, because until then
 * we can't wait on a normal LWKT port (the client's reply port)
 */
void *
caps_client_waitreply(caps_port_t port, lwkt_msg_t msg)
{
    return(caps_client_waitport(&port->lport, msg));
}

/*
 * Wait for a new message to arrive.  At the moment we expect only replies
 * to previously sent messagse to be received.
 */
static
void *
caps_client_waitport(lwkt_port_t lport, lwkt_msg_t wmsg)
{
    caps_port_t port;
    lwkt_msg_t msg;

    /*
     * Wait for any message or a particular message.  If the message
     * is available dequeue and return it.
     */
    if (wmsg == NULL)
	wmsg = TAILQ_FIRST(&lport->mp_msgq);
    if (wmsg && (wmsg->ms_flags & MSGF_DONE)) {
	if (wmsg->ms_flags & MSGF_QUEUED) {
	    TAILQ_REMOVE(&lport->mp_msgq, wmsg, ms_node);
	    wmsg->ms_flags &= ~MSGF_QUEUED;
	}
	return(wmsg);
    }

    /*
     * Wait for any message or a particular message which is not yet
     * available.
     */
    port = (caps_port_t)lport;
    for (;;) {
	struct kevent kev;

	if (kevent(port->kqfd, NULL, 0, &kev, 1, NULL) <= 0) {
	    if (errno == EINTR)
		continue;
	    msg = NULL;
	    break;
	}
	msg = caps_client_kev(&kev);
	if (msg != NULL) {
	    assert(msg->ms_flags & MSGF_DONE);
	    /*
	     * Return the message we are looking for
	     */
	    if (msg == wmsg || wmsg == NULL) {
		break;
	    }
	    /*
	     * Or save it for later retrieval
	     */
	    TAILQ_INSERT_TAIL(&lport->mp_msgq, msg, ms_node);
	    msg->ms_flags |= MSGF_QUEUED;
	}
    }
    return(msg);
}

/*
 * A message's reply port is set to the port representing the connection
 * the message came in on, not set to the services port.  There should be
 * no replies made to the services port.
 */
static
void
caps_client_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    assert(0);
}

