/*
 *
 * ===================================
 * HARP  |  Host ATM Research Platform
 * ===================================
 *
 *
 * This Host ATM Research Platform ("HARP") file (the "Software") is
 * made available by Network Computing Services, Inc. ("NetworkCS")
 * "AS IS".  NetworkCS does not provide maintenance, improvements or
 * support of any kind.
 *
 * NETWORKCS MAKES NO WARRANTIES OR REPRESENTATIONS, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE, AS TO ANY ELEMENT OF THE
 * SOFTWARE OR ANY SUPPORT PROVIDED IN CONNECTION WITH THIS SOFTWARE.
 * In no event shall NetworkCS be responsible for any damages, including
 * but not limited to consequential damages, arising from or relating to
 * any use of the Software or related support.
 *
 * Copyright 1994-1998 Network Computing Services, Inc.
 *
 * Copies of this Software may be made, however, the above copyright
 * notice must be reproduced on all copies.
 *
 *	@(#) $FreeBSD: src/sys/netatm/kern_include.h,v 1.3 1999/08/28 00:48:40 peter Exp $
 *	@(#) $DragonFly: src/sys/netproto/atm/kern_include.h,v 1.7 2005/06/02 21:36:06 dillon Exp $
 *
 */

/*
 * Core ATM Services
 * -----------------
 *
 * Common kernel module includes
 *
 */

#ifndef _NETATM_KERN_INCLUDE_H
#define	_NETATM_KERN_INCLUDE_H

/*
 * Note that we're compiling kernel code
 */
#define	ATM_KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/sockio.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/resourcevar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syslog.h>
#include <sys/eventhandler.h>

#include <sys/thread2.h>
#include <sys/mplock2.h>
#include <sys/msgport2.h>

#include <machine/clock.h>
#include <vm/vm.h>
#include <vm/pmap.h>

/*
 * Networking support
 */
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/netisr.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>

/*
 * Porting fluff
 */
#include "port.h"

/*
 * ATM core services
 */
#include "queue.h"
#include "atm.h"
#include "atm_sys.h"
#include "atm_sap.h"
#include "atm_cm.h"
#include "atm_if.h"
#include "atm_vc.h"
#include "atm_ioctl.h"
#include "atm_sigmgr.h"
#include "atm_stack.h"
#include "atm_pcb.h"
#include "atm_var.h"

#endif	/* _NETATM_KERN_INCLUDE_H */
