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
 *	@(#) $FreeBSD: src/sys/dev/hfa/fore_intr.c,v 1.3 1999/08/28 00:41:50 peter Exp $
 *	@(#) $DragonFly: src/sys/dev/atm/hfa/fore_intr.c,v 1.5 2005/02/01 00:51:50 joerg Exp $
 */

/*
 * FORE Systems 200-Series Adapter Support
 * ---------------------------------------
 *
 * Interrupt processing
 *
 */

#include "fore_include.h"

/*
 * Device interrupt routine
 * 
 * Called at interrupt level.
 *
 * Arguments:
 *	arg		pointer to device unit structure
 *
 * Returns:
 *	1 		device interrupt was serviced
 *	0		no interrupts serviced
 *
 */
void
fore_intr(arg)
	void	*arg;
{
	Fore_unit	*fup = arg;
	Aali	*aap;

	/*
	 * Try to prevent stuff happening after we've paniced
	 */
	if (panicstr)
		return;

	/*
	 * Get to the microcode shared memory interface
	 */
	if ((aap = fup->fu_aali) == NULL)
		return;

	/*
	 * Has this card issued an interrupt??
	 */
	if (*fup->fu_psr) {
		/*
		 * Clear the device interrupt
		 */
		switch (fup->fu_config.ac_device) {

		case DEV_FORE_PCA200E:
			PCA200E_HCR_SET(*fup->fu_ctlreg, PCA200E_CLR_HBUS_INT);
			break;
		default:
			panic("fore_intr: unknown device type");
		}
		aap->aali_intr_sent = CP_WRITE(0);

		/*
		 * Reset the watchdog timer
		 */
		fup->fu_timer = FORE_WATCHDOG;

		/*
		 * Device initialization handled separately
		 */
		if ((fup->fu_flags & CUF_INITED) == 0) {

			/*
			 * We're just initializing device now, so see if
			 * the initialization command has completed
			 */
			if (CP_READ(aap->aali_init.init_status) & 
						QSTAT_COMPLETED)
				fore_initialize_complete(fup);

			/*
			 * If we're still not inited, none of the host
			 * queues are setup yet
			 */
			if ((fup->fu_flags & CUF_INITED) == 0)
				return;
		}

		/*
		 * Drain the queues of completed work
		 */
		fore_cmd_drain(fup);
		fore_recv_drain(fup);
		fore_xmit_drain(fup);

		/*
		 * Supply more buffers to the CP
		 */
		fore_buf_supply(fup);
	}
}


/*
 * Watchdog timeout routine
 * 
 * Called when we haven't heard from the card in a while.  Just in case
 * we missed an interrupt, we'll drain the queues and try to resupply the
 * CP with more receive buffers.  If the CP is partially wedged, hopefully
 * this will be enough to get it going again.
 *
 * Called with interrupts locked out.
 *
 * Arguments:
 *	fup		pointer to device unit structure
 *
 * Returns:
 *	none
 *
 */
void
fore_watchdog(fup)
	Fore_unit	*fup;
{
	/*
	 * Try to prevent stuff happening after we've paniced
	 */
	if (panicstr) {
		return;
	}

	/*
	 * Reset the watchdog timer
	 */
	fup->fu_timer = FORE_WATCHDOG;

	/*
	 * If the device is initialized, nudge it (wink, wink)
	 */
	if (fup->fu_flags & CUF_INITED) {

		/*
		 * Drain the queues of completed work
		 */
		fore_cmd_drain(fup);
		fore_recv_drain(fup);
		fore_xmit_drain(fup);

		/*
		 * Supply more buffers to the CP
		 */
		fore_buf_supply(fup);
	}

	return;
}
