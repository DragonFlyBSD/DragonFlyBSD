#ifndef _NET_IF_POLL_H_
#define _NET_IF_POLL_H_

#ifdef _KERNEL

struct lwkt_serialize;
struct ifnet;

typedef	void	(*ifpoll_iofn_t)(struct ifnet *, void *, int);
typedef	void	(*ifpoll_stfn_t)(struct ifnet *, int);

struct ifpoll_status {
	struct lwkt_serialize	*serializer;
	ifpoll_stfn_t		status_func;
};

struct ifpoll_io {
	struct lwkt_serialize	*serializer;
	void			*arg;
	ifpoll_iofn_t		poll_func;
};

struct ifpoll_info {
	struct ifnet		*ifpi_ifp;
	struct ifpoll_status	ifpi_status;
	struct ifpoll_io	ifpi_rx[MAXCPU];
	struct ifpoll_io	ifpi_tx[MAXCPU];
};

#endif	/* _KERNEL */

#endif	/* !_NET_IF_POLL_H_ */
