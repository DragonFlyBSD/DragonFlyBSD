/*
 * SYS/MSGPORT.H
 *
 *	Implements LWKT messages and ports.
 * 
 * $DragonFly: src/sys/sys/msgport.h,v 1.32 2008/11/26 15:05:42 sephe Exp $
 */

#ifndef _SYS_MSGPORT_H_
#define _SYS_MSGPORT_H_

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>		/* TAILQ_* macros */
#endif
#ifndef _SYS_STDINT_H_
#include <sys/stdint.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif

#ifdef _KERNEL

#ifndef _SYS_MALLOC_H_
#include <sys/malloc.h>
#endif

#endif

struct lwkt_msg;
struct lwkt_port;
struct lwkt_serialize;
struct thread;

typedef struct lwkt_msg		*lwkt_msg_t;
typedef struct lwkt_port	*lwkt_port_t;

typedef TAILQ_HEAD(lwkt_msg_queue, lwkt_msg) lwkt_msg_queue;

/*
 * The standard message and port structure for communications between
 * threads.  See kern/lwkt_msgport.c for documentation on how messages and
 * ports work.
 *
 * A message may only be manipulated by whomever currently owns it,
 * which generally means the originating port if the message has
 * not been sent yet or has been replied, and the target port if the message
 * has been sent and/or is undergoing processing.
 *
 * NOTE! 64-bit-align this structure.
 */
typedef struct lwkt_msg {
    TAILQ_ENTRY(lwkt_msg) ms_node;	/* link node */
    lwkt_port_t ms_target_port;		/* current target or relay port */
    lwkt_port_t	ms_reply_port;		/* async replies returned here */
    void	(*ms_abortfn)(struct lwkt_msg *);
    int		ms_flags;		/* message flags */
    int		ms_error;		/* positive error code or 0 */
    union {
	void	*ms_resultp;		/* misc pointer data or result */
	int	ms_result;		/* standard 'int'eger result */
	long	ms_lresult;		/* long result */
	int	ms_fds[2];		/* two int bit results */
	__int32_t ms_result32;		/* 32 bit result */
	__int64_t ms_result64;		/* 64 bit result */
	__off_t	ms_offset;		/* off_t result */
    } u;
    int		ms_pad[2];		/* future use */
} lwkt_msg;

/*
 * Message state flags are manipulated by the current owner only.
 *
 * DONE		Indicates completion of the reply.  This flag is also set
 *		for unsent messages.
 *
 * REPLY	Indicates message is being replied but may or may not
 *		have been queued or returned yet.  This bit is left set
 *		when a message is retrieved from a reply port so the caller
 *		can distinguish between requests and replies.
 *
 * QUEUED	Indicates message is queued on reply or target port, or
 *		some other port.
 *
 * SYNC		Indicates that the originator is blocked directly on the
 *		message and that the message should be signaled on
 *		completion instead of queued.
 *
 * INTRANSIT	Indicates that the message state is indeterminant (e.g.
 *		being passed through an IPI).
 *
 * ABORTABLE	Static flag indicates that ms_abortfn is valid.
 *
 * High 16 bits are available to message handlers.
 */
#define MSGF_DONE	0x0001		/* message is complete */
#define MSGF_REPLY	0x0002		/* asynch message has been returned */
#define MSGF_QUEUED	0x0004		/* message has been queued sanitychk */
#define MSGF_SYNC	0x0008		/* synchronous message operation */
#define MSGF_INTRANSIT	0x0010		/* in-transit (IPI) */
#define MSGF_WAITING	0x0020		/* MSGF_SYNC being waited upon */
#define MSGF_DROPABLE	0x0040		/* message supports drop */
#define MSGF_ABORTABLE	0x0080		/* message supports abort */
#define MSGF_PRIORITY	0x0100		/* priority message */

#define MSGF_USER0	0x00010000
#define MSGF_USER1	0x00020000
#define MSGF_USER2	0x00040000
#define MSGF_USER3	0x00080000

#define MSG_CMD_CDEV	0x00010000
#define MSG_CMD_VFS	0x00020000
#define MSG_CMD_SYSCALL	0x00030000
#define MSG_SUBCMD_MASK	0x0000FFFF

#ifdef _KERNEL
MALLOC_DECLARE(M_LWKTMSG);
#endif

/*
 * Notes on port processing requirements:
 *
 * mp_putport():
 *	- may return synchronous error code (error != EASYNC) directly and
 *	  does not need to check or set MSGF_DONE if so, or set ms_target_port
 *	- for asynch procesing should clear MSGF_DONE and set ms_target_port
 *	  to port prior to initiation of the command.
 *
 * mp_waitmsg():
 *	- wait for a particular message to be returned.
 *
 * mp_waitport():
 *	- wait for a new message on the specified port.
 *
 * mp_replyport():
 *	- reply a message (executed on the originating port to return a
 *	  message to it).  This can be rather involved if abort is to be
 *	  supported, see lwkt_default_replyport().  Generally speaking
 *	  one sets MSGF_DONE and MSGF_REPLY.  If MSGF_SYNC is set the message
 *	  is not queued to the port and the reply code wakes up the waiter
 *	  directly.
 *
 * mp_dropmsg():
 *	- drop a specific message from the specified port.  Currently only
 *	  threads' embedded ports (thread ports or spin ports) support this
 *        function and must be used in the port's owner thread.
 *	  (returns 0 on success, ENOENT on error).
 *
 * The use of mpu_td and mp_u.spin is specific to the port callback function
 * set.  Default ports are tied to specific threads and use cpu locality
 * of reference and mpu_td (and not mp_u.spin at all).  Descriptor ports
 * assume access via descriptors, signal interruption, etc.  Such ports use
 * mp_u.spin (and not mpu_td at all) and may be accessed by multiple threads.
 *
 * Threads' embedded ports always have mpu_td back pointing to themselves.
 */
typedef struct lwkt_port {
    lwkt_msg_queue	mp_msgq;
    lwkt_msg_queue	mp_msgq_prio;
    int			mp_flags;
    int			mp_cpuid;
    union {
	struct spinlock	spin;
	struct lwkt_serialize *serialize;
	void		*data;
    } mp_u;
    struct thread	*mpu_td;
    void *		(*mp_getport)(lwkt_port_t);
    int			(*mp_putport)(lwkt_port_t, lwkt_msg_t);
    int			(*mp_waitmsg)(lwkt_msg_t, int flags);
    void *		(*mp_waitport)(lwkt_port_t, int flags);
    void		(*mp_replyport)(lwkt_port_t, lwkt_msg_t);
    int			(*mp_dropmsg)(lwkt_port_t, lwkt_msg_t);
    int			(*mp_putport_oncpu)(lwkt_port_t, lwkt_msg_t);
} lwkt_port;

#ifdef _KERNEL

#define mpu_spin	mp_u.spin
#define mpu_serialize	mp_u.serialize
#define mpu_data	mp_u.data

#endif

/*
 * Port state flags.
 *
 * WAITING      The owner of the port is descheduled waiting for a message
 *              to be replied.  In case this a spin port there can actually
 *              be more than one thread waiting on the port.
 */
#define MSGPORTF_WAITING	0x0001

/*
 * These functions are good for userland as well as the kernel.  The
 * messaging function support for userland is provided by the kernel's
 * kern/lwkt_msgport.c.  The port functions are provided by userland.
 */

void lwkt_initport_thread(lwkt_port_t, struct thread *);
void lwkt_initport_spin(lwkt_port_t, struct thread *, boolean_t);
void lwkt_initport_serialize(lwkt_port_t, struct lwkt_serialize *);
void lwkt_initport_panic(lwkt_port_t);
void lwkt_initport_replyonly_null(lwkt_port_t);
void lwkt_initport_replyonly(lwkt_port_t,
				void (*rportfn)(lwkt_port_t, lwkt_msg_t));
void lwkt_initport_putonly(lwkt_port_t,
				int (*pportfn)(lwkt_port_t, lwkt_msg_t));

void lwkt_sendmsg(lwkt_port_t, lwkt_msg_t);
void lwkt_sendmsg_oncpu(lwkt_port_t, lwkt_msg_t);
void lwkt_sendmsg_prepare(lwkt_port_t, lwkt_msg_t);
void lwkt_sendmsg_start(lwkt_port_t, lwkt_msg_t);
int lwkt_domsg(lwkt_port_t, lwkt_msg_t, int);
int lwkt_forwardmsg(lwkt_port_t, lwkt_msg_t);
void lwkt_abortmsg(lwkt_msg_t);

#endif
