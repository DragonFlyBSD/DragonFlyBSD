/*
 * SYS/CAPS.H
 *
 *	Implements an architecture independant Capability Service API
 * 
 * $DragonFly: src/sys/sys/caps.h,v 1.6 2004/03/14 11:04:12 hmp Exp $
 */

#ifndef _SYS_CAPS_H_
#define _SYS_CAPS_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_MSGPORT_H_
#include <sys/msgport.h>
#endif

typedef enum caps_msg_state { 
	CAPMS_REQUEST, 
	CAPMS_REQUEST_RETRY, 	/* internal / FUTURE */
	CAPMS_REPLY, 
	CAPMS_REPLY_RETRY,	/* internal / FUTURE */
	CAPMS_DISPOSE
} caps_msg_state_t;

typedef struct caps_msgid {
	off_t			c_id;
	caps_msg_state_t	c_state;
	int			c_reserved01;
} *caps_msgid_t;

typedef enum caps_type { 
	CAPT_UNKNOWN, CAPT_CLIENT, CAPT_SERVICE, CAPT_REMOTE, CAPT_FORKED
} caps_type_t;

typedef int64_t	caps_gen_t;

/*
 * Note: upper 16 bits reserved for kernel use
 */
#define CAPF_UFLAGS	0xFFFF
#define CAPF_USER	0x0001
#define CAPF_GROUP	0x0002
#define CAPF_WORLD	0x0004
#define CAPF_EXCL	0x0008
#define CAPF_ANYCLIENT	(CAPF_USER|CAPF_GROUP|CAPF_WORLD)
#define CAPF_WCRED	0x0010	/* waiting for cred */
#define CAPF_NOFORK	0x0020	/* do not create a dummy entry on fork */
#define CAPF_WAITSVC	0x0040	/* block if service not available */
/* FUTURE: CAPF_ASYNC - support async services */
/* FUTURE: CAPF_NOGROUPS - don't bother filling in the groups[] array */
/* FUTURE: CAPF_TERM - send termination request to existing service */
/* FUTURE: CAPF_TAKE - take over existing service's connections */
/* FUTURE: CAPF_DISPOSE_IMM - need immediate dispose wakeups */

/*
 * Abort codes
 */
#define CAPS_ABORT_NOTIMPL	0	/* abort not implemented, no action */
#define CAPS_ABORT_RETURNED	1	/* already returned, no action */
#define CAPS_ABORT_BEFORESERVER	2	/* caught before the server got it */
#define CAPS_ABORT_ATSERVER	3	/* server had retrieved message */

#define CAPF_ABORT_HARD		0x0001	/* rip out from under server (3) */

#define CAPS_MAXGROUPS	16
#define CAPS_MAXNAMELEN	64
#define CAPS_MAXINPROG	128

struct thread;

typedef struct caps_port {
	struct lwkt_port	cp_lport;
	int			cp_portid;	/* caps port id */
	int			cp_upcallid;	/* upcall id */
} *caps_port_t;

typedef struct caps_cred {
	pid_t			pid;
	uid_t			uid;
	uid_t			euid;
	gid_t			gid;
	int			ngroups;
	int			cacheid;
	gid_t			groups[CAPS_MAXGROUPS];
} *caps_cred_t;

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

struct caps_kmsg;

TAILQ_HEAD(caps_kmsg_queue, caps_kmsg);

/*
 * caps_kinfo -	Holds a client or service registration
 *
 * ci_msgpendq: holds the kernel copy of the message after it has been
 * 		sent to the local port.  The message is matched up against
 *		replies and automatically replied if the owner closes its 
 *		connection.
 */
typedef struct caps_kinfo {
	struct lwkt_port	ci_lport;	/* embedded local port */
	struct caps_kinfo	*ci_tdnext;	/* per-process list */
	struct caps_kinfo	*ci_hnext;	/* registration hash table */
	struct thread		*ci_td;		/* owner */
	struct caps_kmsg_queue	ci_msgpendq;	/* pending reply (just rcvd) */
	struct caps_kmsg_queue	ci_msguserq;	/* pending reply (user holds) */
	struct caps_kinfo	*ci_rcaps;	/* connected to remote */
	int			ci_cmsgcount;	/* client in-progress msgs */
	int			ci_id;
	int			ci_flags;
	int			ci_refs;
	int			ci_mrefs;	/* message (vmspace) refs */
	caps_type_t		ci_type;
	caps_gen_t		ci_gen;
	uid_t			ci_uid;
	gid_t			ci_gid;
	int			ci_namelen;
	char			ci_name[4];	/* variable length */
	/* ci_name must be last element */
} *caps_kinfo_t;

/* note: user flags are held in the low 16 bits */
#define CAPKF_TDLIST	0x00010000
#define CAPKF_HLIST	0x00020000
#define CAPKF_FLUSH	0x00040000
#define CAPKF_RCAPS	0x00080000
#define CAPKF_CLOSED	0x00100000
#define CAPKF_MWAIT	0x00200000

/*
 * Kernel caps message.  The kernel keepps track of messagse received,
 * undergoing processing by the service, and returned.  User-supplied data
 * is copied on reception rather then transmission.
 */
typedef struct caps_kmsg {
	TAILQ_ENTRY(caps_kmsg)	km_node;
	caps_kinfo_t		km_mcaps;	/* message sender */
	void			*km_umsg;	/* mcaps vmspace */
	int			km_umsg_size;	/* mcaps vmspace */
	struct caps_cred	km_ccr;		/* caps cred for msg */
	struct caps_msgid	km_msgid;
	int			km_flags;
} *caps_kmsg_t;

#define km_state	km_msgid.c_state

#define CAPKMF_ONUSERQ		0x0001
#define CAPKMF_ONPENDQ		0x0002
#define CAPKMF_REPLY		0x0004
#define CAPKMF_CDONE		0x0008
#define CAPKMF_PEEKED		0x0010
#define CAPKMF_ABORTED		0x0020

#endif

#ifdef _KERNEL

/*
 * kernel support
 */
void caps_exit(struct thread *td);
void caps_fork(struct proc *p1, struct proc *p2, int flags);

#else

/*
 * Userland API (libcaps)
 */
caps_port_t caps_service(const char *, uid_t, gid_t, mode_t, int);
caps_port_t caps_client(const char *, uid_t, gid_t, int);

/*
 * Syscall API
 */
int caps_sys_service(const char *, uid_t, gid_t, int, int);
int caps_sys_client(const char *, uid_t, gid_t, int, int);
off_t caps_sys_put(int, void *, int);
int caps_sys_reply(int, void *, int, off_t);
int caps_sys_get(int, void *, int, caps_msgid_t, caps_cred_t);
int caps_sys_wait(int, void *, int, caps_msgid_t, caps_cred_t);
int caps_sys_abort(int, off_t, int);
int caps_sys_setgen(int, caps_gen_t);
caps_gen_t caps_sys_getgen(int);

#endif

#endif

