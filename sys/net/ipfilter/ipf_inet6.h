/* $DragonFly: src/sys/net/ipfilter/ipf_inet6.h,v 1.1 2006/12/18 23:26:36 pavalos Exp $ */
/* This file uses the kernel config to determine if IPV6 should be used. */

#include "opt_inet6.h"

#ifdef INET6
#define USE_INET6	1
#endif
