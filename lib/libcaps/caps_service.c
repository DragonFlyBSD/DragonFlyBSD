/*
 * CAPS_SERVICE.C
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
 * $DragonFly: src/lib/libcaps/Attic/caps_service.c,v 1.1 2003/11/24 21:15:58 dillon Exp $
 */

#include "defs.h"

static lwkt_msg_t caps_service_kev(struct kevent *kev);

static int caps_service_putport(lwkt_port_t port, lwkt_msg_t msg);
static void *caps_service_waitport(lwkt_port_t port, lwkt_msg_t msg);
static void caps_service_replyport(lwkt_port_t port, lwkt_msg_t msg);

static int caps_remote_putport(lwkt_port_t port, lwkt_msg_t msg);
static void *caps_remote_waitport(lwkt_port_t port, lwkt_msg_t msg);
static void caps_remote_replyport(lwkt_port_t port, lwkt_msg_t msg);

/*
 * Create a message port rendezvous at the specified service name.
 *
 * Use the standard lwkt_*() messaging functions to retrieve and reply to
 * messages sent to your port.
 *
 * This code will eventually be replaced by a kernel API
 */
caps_port_t
caps_service(const char *name, gid_t gid, mode_t modes, int flags)
{
    struct sockaddr_un sunix;
    struct kevent kev;
    caps_port_t port;
    mode_t omask;
    int len;
    int error;
    uid_t uid;

    /*
     * Allocate a port to handle incoming messages
     */
    port = caps_mkport(CAPT_SERVICE, 
			caps_service_putport,
			caps_service_waitport,
			caps_service_replyport);
    uid = getuid();

    /*
     * Construct the unix domain socket.
     */
    if ((port->lfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
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

    /*
     * If CAPS_EXCL is set do not allow duplicate registrations.
     */
    if (flags & CAPS_EXCL) {
	error = connect(port->lfd, (void *)&sunix,
		offsetof(struct sockaddr_un, sun_path[len+1]));
	if (error == 0) {
	    errno = EEXIST;
	    goto failed;
	}
    }

    /*
     * Bind and listen on the port.  Note: if the unix domain file
     * is on a read-only filesystem the remove may fail.
     */
    remove(sunix.sun_path);	/* XXX use create/rename for atomicy */
    omask = umask(~modes & 0x0777);
    error = bind(port->lfd, (void *)&sunix,
		offsetof(struct sockaddr_un, sun_path[len+1]));
    umask(omask);
    if (error < 0)
	goto failed;
    fcntl(port->lfd, F_SETFL, O_NONBLOCK);
    if ((error = listen(port->lfd, 128)) < 0)
	goto failed;
    if ((port->kqfd = kqueue()) < 0)
	goto failed;

    /*
     * Use kqueue to get accept events on the listen descriptor
     */
    EV_SET(&kev, port->lfd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, port);
    if (kevent(port->kqfd, &kev, 1, NULL, 0, NULL) < 0)
	goto failed;
    DBPRINTF(("Successfully created port %s\n", sunix.sun_path));
    return(port);
failed:
    caps_close(port);
    return(NULL);
}

/*
 * Service kqueue events on a services port.  Note that ports representing
 * individual client connections aggregate their events onto the main services
 * port.
 */
static lwkt_msg_t
caps_service_kev(struct kevent *kev)
{
    caps_port_t port = kev->udata;
    lwkt_msg_t msg;

    if (port->type == CAPT_SERVICE && kev->filter == EVFILT_READ) {
	/*
	 * Accept a new connection on the master service port
	 */
	int fd = accept(port->lfd, NULL, 0);
	caps_port_t rport;

	if (fd >= 0) {
	    rport = caps_mkport(CAPT_REMOTE,
			    caps_remote_putport,
			    caps_remote_waitport,
			    caps_remote_replyport);
	    rport->flags |= CAPPF_WAITCRED | CAPPF_ONLIST;
	    rport->cfd = fd;
	    rport->server = port;
	    fcntl(port->cfd, F_SETFL, O_NONBLOCK);
	    /* note: use rport's mp_refs (1) to indicate ONLIST */
	    TAILQ_INSERT_TAIL(&port->clist, rport, centry);
	    EV_SET(kev, fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, rport);
	    if (kevent(port->kqfd, kev, 1, NULL, 0, NULL) < 0)
		caps_shutdown(rport);
	    else
		DBPRINTF(("accepted %d\n", fd));
	}
	msg = NULL;
    } else if (port->type == CAPT_REMOTE && kev->filter == EVFILT_READ) {
	msg = caps_kev_read(port);
    } else if (port->type == CAPT_REMOTE && kev->filter == EVFILT_WRITE) {
	caps_kev_write(port, NULL);
	msg = NULL;
    } else {
	fprintf(stderr, "bad port: type %d flags %d\n", port->type, kev->filter);
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
caps_service_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    return(EINVAL);
}

/*
 * Wait for a new message to arrive.  Since messages are not replied to
 * services port msg must be NULL.
 */
static
void *
caps_service_waitport(lwkt_port_t lport, lwkt_msg_t msg)
{
    caps_port_t port;

    if (msg)
	return(NULL);
    if ((msg = TAILQ_FIRST(&lport->mp_msgq)) != NULL) {
	TAILQ_REMOVE(&lport->mp_msgq, msg, ms_node);
	return(msg);
    }
    port = (caps_port_t)lport;
    do {
	struct kevent kev;
	while (kevent(port->kqfd, NULL, 0, &kev, 1, NULL) > 0) {
	    if ((msg = (lwkt_msg_t)caps_service_kev(&kev)) != NULL)
		return(msg);
	}
    } while (errno == EINTR);
    return(NULL);
}

/*
 * A message's reply port is set to the port representing the connection
 * the message came in on, not set to the services port.  There should be
 * no replies made to the services port.
 */
static
void
caps_service_replyport(lwkt_port_t port, lwkt_msg_t msg)
{
    assert(0);
}

/*
 * You can only reply to messages received on a services remote port. 
 * You cannot send a message to a services remote port.   XXX future 
 * feature should allow the sending of unsolicited messagse to the remote
 * client.
 */
static
int
caps_remote_putport(lwkt_port_t port, lwkt_msg_t msg)
{
    return(EINVAL);
}

/*
 * Wait for a new message to arrive.  Message waiting is done on the main
 * services port, not on the per-client remote port.  XXX as a future feature
 * we could allow waiting for a message from a particular client.
 */
static
void *
caps_remote_waitport(lwkt_port_t lport, lwkt_msg_t msg)
{
    assert(0);
    return(NULL);
}

/*
 * Messages received on the main services port are replied back to a
 * port representing the particular connection the message actually
 * came in on.  If the port has been shutdown due to an error we have
 * to destroy it when the last message reference goes away.
 */
static
void
caps_remote_replyport(lwkt_port_t lport, lwkt_msg_t msg)
{
    assert(lport->mp_refs > 0);
    --lport->mp_refs;
    if (lport->mp_flags & CAPPF_SHUTDOWN) {
	if (lport->mp_refs == 0) {
	    ++lport->mp_refs;
	    caps_close((caps_port_t)lport);
	}
	free(msg);
    } else {
	msg->ms_flags |= MSGF_DONE | MSGF_REPLY;
	caps_kev_write((caps_port_t)lport, msg);
    }
}

