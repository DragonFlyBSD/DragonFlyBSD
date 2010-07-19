/*
 * Copyright (c) 1997, 2000 Hellmuth Michaelis. All rights reserved.
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
 *---------------------------------------------------------------------------
 *
 *	i4b_ctl.c - i4b system control port driver
 *	------------------------------------------
 *
 *	$Id: i4b_ctl.c,v 1.37 2000/05/31 08:04:43 hm Exp $
 *
 * $FreeBSD: src/sys/i4b/driver/i4b_ctl.c,v 1.10.2.3 2001/08/12 16:22:48 hm Exp $
 * $DragonFly: src/sys/net/i4b/driver/i4b_ctl.c,v 1.14 2006/12/22 23:44:55 swildner Exp $
 *
 *	last edit-date: [Sat Aug 11 18:06:38 2001]
 *
 *---------------------------------------------------------------------------*/

#include "use_i4bctl.h"

#if NI4BCTL > 1
#error "only 1 (one) i4bctl device allowed!"
#endif

#if NI4BCTL > 0

#include <sys/param.h>

#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <net/if.h>

#include <net/i4b/include/machine/i4b_debug.h>
#include <net/i4b/include/machine/i4b_ioctl.h>

#include "../include/i4b_global.h"
#include "../include/i4b_l3l4.h"
#include "../layer2/i4b_l2.h"

static int openflag = 0;

static	d_open_t	i4bctlopen;
static	d_close_t	i4bctlclose;
static	d_ioctl_t	i4bctlioctl;

#define CDEV_MAJOR 55

static struct dev_ops i4bctl_ops = {
	{ "i4bctl", CDEV_MAJOR, 0 },
	.d_open =      i4bctlopen,
	.d_close =     i4bctlclose,
	.d_ioctl =     i4bctlioctl,
};

static void i4bctlattach(void *);
PSEUDO_SET(i4bctlattach, i4b_i4bctldrv);

#define PDEVSTATIC	static

/*---------------------------------------------------------------------------*
 *	interface attach routine
 *---------------------------------------------------------------------------*/
PDEVSTATIC void
i4bctlattach(void *dummy)
{
#ifndef HACK_NO_PSEUDO_ATTACH_MSG
	kprintf("i4bctl: ISDN system control port attached\n");
#endif
}

/*---------------------------------------------------------------------------*
 *	i4bctlopen - device driver open routine
 *---------------------------------------------------------------------------*/
PDEVSTATIC int
i4bctlopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	if(minor(dev))
		return (ENXIO);
	if(openflag)
		return (EBUSY);
	openflag = 1;
	return (0);
}

/*---------------------------------------------------------------------------*
 *	i4bctlclose - device driver close routine
 *---------------------------------------------------------------------------*/
PDEVSTATIC int
i4bctlclose(struct dev_close_args *ap)
{
	openflag = 0;
	return (0);
}

/*---------------------------------------------------------------------------*
 *	i4bctlioctl - device driver ioctl routine
 *---------------------------------------------------------------------------*/
PDEVSTATIC int
i4bctlioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
#if DO_I4B_DEBUG
	ctl_debug_t *cdbg;	
	int error = 0;
#endif
	
#if !DO_I4B_DEBUG
       return(ENODEV);
#else
	if(minor(dev))
		return(ENODEV);

	switch(ap->a_cmd)
	{
		case I4B_CTL_GET_DEBUG:
			cdbg = (ctl_debug_t *)ap->a_data;
			cdbg->l1 = i4b_l1_debug;
			cdbg->l2 = i4b_l2_debug;
			cdbg->l3 = i4b_l3_debug;
			cdbg->l4 = i4b_l4_debug;
			break;
		
		case I4B_CTL_SET_DEBUG:
			cdbg = (ctl_debug_t *)ap->a_data;
			i4b_l1_debug = cdbg->l1;
			i4b_l2_debug = cdbg->l2;
			i4b_l3_debug = cdbg->l3;
			i4b_l4_debug = cdbg->l4;
			break;

                case I4B_CTL_GET_CHIPSTAT:
                {
                        struct chipstat *cst;
			cst = (struct chipstat *)ap->a_data;
			(*ctrl_desc[cst->driver_unit].N_MGMT_COMMAND)(cst->driver_unit, CMR_GCST, cst);
                        break;
                }

                case I4B_CTL_CLR_CHIPSTAT:
                {
                        struct chipstat *cst;
			cst = (struct chipstat *)ap->a_data;
			(*ctrl_desc[cst->driver_unit].N_MGMT_COMMAND)(cst->driver_unit, CMR_CCST, cst);
                        break;
                }

                case I4B_CTL_GET_LAPDSTAT:
                {
                        l2stat_t *l2s;
                        l2_softc_t *sc;
                        l2s = (l2stat_t *)ap->a_data;

                        if( l2s->unit < 0 || l2s->unit > MAXL1UNITS)
                        {
                        	error = EINVAL;
				break;
			}
			  
			sc = &l2_softc[l2s->unit];

			bcopy(&sc->stat, &l2s->lapdstat, sizeof(lapdstat_t));
                        break;
                }

                case I4B_CTL_CLR_LAPDSTAT:
                {
                        int *up;
                        l2_softc_t *sc;
                        up = (int *)ap->a_data;

                        if( *up < 0 || *up > MAXL1UNITS)
                        {
                        	error = EINVAL;
				break;
			}
			  
			sc = &l2_softc[*up];

			bzero(&sc->stat, sizeof(lapdstat_t));
                        break;
                }

		default:
			error = ENOTTY;
			break;
	}
	return(error);
#endif /* DO_I4B_DEBUG */
}

#endif /* NI4BCTL > 0 */
