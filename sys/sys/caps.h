/*
 * SYS/CAPS.H
 *
 *	Implements an architecture independant Capability Service API
 * 
 * $DragonFly: src/sys/sys/caps.h,v 1.1 2003/11/24 21:15:54 dillon Exp $
 */

#ifndef _SYS_CAPS_H_
#define _SYS_CAPS_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>
#endif

#define CAPS_USER	0x00000001
#define CAPS_GROUP	0x00000002
#define CAPS_WORLD	0x00000004
#define CAPS_EXCL	0x00000008
#define CAPS_ANYCLIENT	(CAPS_USER|CAPS_GROUP|CAPS_WORLD)
#define CAPS_WCRED	0x00000010	/* waiting for cred */

/*
 * caps_type associated with caps_port:
 *
 *	CAPT_CLIENT	port returned to client representing connection to
 *			service.
 *	CAPT_SERVICE	port returned to service representing namespace
 *	CAPT_REMOTE	temporary port used by service to represent
 *			client connections to service (set as replyport for
 *			messages)
 *
 */
enum caps_type { CAPT_UNKNOWN, CAPT_CLIENT, CAPT_SERVICE, CAPT_REMOTE };

#define CAPS_MAXGROUPS	16

struct thread;
struct caps_port;

typedef struct caps_port *caps_port_t;

struct caps_cred {
	pid_t			pid;
	uid_t			uid;
	uid_t			euid;
	gid_t			gid;
	int			ngroups;
	gid_t			groups[CAPS_MAXGROUPS];
};

struct caps_port {
	struct lwkt_port	lport;
	caps_port_t		server;	/* if CAPT_REMOTE, pointer to server */
	enum caps_type		type;
	int			kqfd;	/* kqueue to collect active connects */
	int			lfd;	/* server: listening on (server) */
	int			cfd;	/* client/remote connection fd */
	int			flags;
	TAILQ_HEAD(, caps_port)	clist;	/* server: client client connections */
	TAILQ_ENTRY(caps_port)	centry;
	TAILQ_HEAD(, lwkt_msg)	wlist;	/* queue of outgoing messages */
	TAILQ_HEAD(, lwkt_msg)	mlist;	/* written message waiting for reply */
	struct lwkt_msg		rmsg_static;
	lwkt_msg_t		rmsg;	/* read message in progress */
	struct caps_cred	cred;	/* cred of owner of port */
	int			rbytes;	/* read in progress byte count */
	int			wbytes;	/* write in progress byte count */
};

#define CAPPF_WAITCRED		0x0001
#define CAPPF_ONLIST		0x0002
#define CAPPF_WREQUESTED	0x0004	/* write event requested */
#define CAPPF_SHUTDOWN		0x0008	/* terminated/failed */

#define CAPMSG_MAXSIZE		(1024+64*1024)

/*
 * API
 */
caps_port_t caps_service(const char *name, gid_t gid, mode_t modes, int flags);
caps_port_t caps_client(const char *name, uid_t uid, int flags);

/*
 * Temporary hack until LWKT threading is integrated.
 */
void *caps_client_waitreply(caps_port_t port, lwkt_msg_t msg);

#endif

