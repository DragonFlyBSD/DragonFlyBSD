.PATH:	${.CURDIR}/inet
SRCS+=	inet_addr.c inet_cidr_ntop.c inet_cidr_pton.c inet_lnaof.c \
	inet_makeaddr.c inet_net_ntop.c inet_net_pton.c inet_neta.c \
	inet_netof.c inet_network.c inet_ntoa.c inet_ntop.c \
	inet_pton.c nsap_addr.c

CMAPS+=	${.CURDIR}/inet/Symbol.map

.PATH:	${.CURDIR}/isc
SRCS+=	ev_streams.c ev_timers.c

CMAPS+=	${.CURDIR}/isc/Symbol.map

.PATH:	${.CURDIR}/nameser
SRCS+=	ns_name.c ns_netint.c ns_parse.c ns_print.c ns_samedomain.c ns_ttl.c

CMAPS+=	${.CURDIR}/nameser/Symbol.map

.PATH:	${.CURDIR}/resolv
SRCS+=	h_errno.c herror.c mtctxres.c res_comp.c res_data.c res_debug.c \
	res_findzonecut.c res_init.c res_mkquery.c res_mkupdate.c \
	res_query.c res_send.c res_state.c res_update.c

CMAPS+=	${.CURDIR}/resolv/Symbol.map
