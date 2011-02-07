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
 *	@(#) $FreeBSD: src/usr.sbin/atm/scspd/scsp_log.c,v 1.3 1999/08/28 01:15:33 peter Exp $
 *	@(#) $DragonFly: src/usr.sbin/atm/scspd/scsp_log.c,v 1.3 2003/11/15 20:33:43 eirikn Exp $
 */


/*
 * Server Cache Synchronization Protocol (SCSP) Support
 * ----------------------------------------------------
 *
 * SCSP logging routines
 *
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netatm/port.h> 
#include <netatm/queue.h> 
#include <netatm/atm.h>
#include <netatm/atm_if.h>
#include <netatm/atm_sap.h>
#include <netatm/atm_sys.h>
#include <netatm/atm_ioctl.h>
  
#include <errno.h>
#include <libatm.h>
#if __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "scsp_msg.h"
#include "scsp_if.h"
#include "scsp_var.h"

/*
 * Global variables
 */
FILE	*scsp_trace_file = NULL;


/*
 * Write a message to SCSP's log
 *
 * Arguments:
 *	level	pointer to an SCSP cache key structure
 *	fmt	printf-style format string
 *	...	parameters for printf-style use according to fmt
 *
 * Returns:
 *	none
 *
 */
void
scsp_log(const int level, const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);

	/*
	 * In debug mode, just write to stdout
	 */
	if (scsp_debug_mode) {
		vprintf(fmt, ap);
		printf("\n");
		return;
	}

	/*
	 * Write to syslog if it's active or if no log file is set up
	 */
	if (scsp_log_syslog || !scsp_log_file) {
		vsyslog(level, fmt, ap);
	}

	/*
	 * Write to the log file if there's one set up
	 */
	if (scsp_log_file) {
		vfprintf(scsp_log_file, fmt, ap);
		fprintf(scsp_log_file, "\n");
	}

	va_end(ap);
}


/*
 * Open SCSP's trace file
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	none
 *
 */
void
scsp_open_trace(void)
{
	char	fname[64];

	/*
	 * Build a file name
	 */
	UM_ZERO(fname, sizeof(fname));
	sprintf(fname, "/tmp/scspd.%d.trace", getpid());

	/*
	 * Open the trace file.  If the open fails, log an error, but
	 * keep going.  The trace routine will notice that the file
	 * isn't open and won't try to write to it.
	 */
	scsp_trace_file = fopen(fname, "w");
	if (scsp_trace_file == NULL) {
		scsp_log(LOG_ERR, "Can't open trace file");
	}
}


/*
 * Write a message to SCSP's trace file
 *
 * Arguments:
 *	fmt	printf-style format string
 *	...	parameters for printf-style use according to fmt
 *
 * Returns:
 *	none
 *
 */
void
scsp_trace(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);

	/*
	 * Write the message to the trace file, if it's open
	 */
	if (scsp_trace_file) {
		vfprintf(scsp_trace_file, fmt, ap);
	}

	va_end(ap);
}


/*
 * Write an SCSP message to SCSP's trace file
 *
 * Arguments:
 *	dcsp	pointer to DCS block for the message
 *	msg	pointer to the message
 *	dir	a direction indicator--0 for sending, 1 for receiving
 *
 * Returns:
 *	none
 *
 */
void
scsp_trace_msg(Scsp_dcs *dcsp, Scsp_msg *msg, int dir)
{
	struct in_addr	addr;

	/*
	 * Copy the remote IP address into a struct in_addr
	 */
	UM_COPY(dcsp->sd_dcsid.id, &addr.s_addr,
			sizeof(struct in_addr));
	
	/*
	 * Write the message to the trace file, if it's open
	 */
	if (scsp_trace_file) {
		scsp_trace("SCSP message at %p %s %s\n",
				msg,
				(dir ? "received from" : "sent to"),
				format_ip_addr(&addr));
		print_scsp_msg(scsp_trace_file, msg);
	}
}


/*
 * Log a memory error and exit
 *
 * Arguments:
 *	cp	message to log
 *
 * Returns:
 *	exits, does not return
 *
 */
void
scsp_mem_err(char *cp)
{
	scsp_log(LOG_CRIT, "out of memory: %s", cp);
	exit(2);
}
