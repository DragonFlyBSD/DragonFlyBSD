/*
 *  Top - a top users display for Berkeley Unix
 *
 *  This file defines the locations on tne screen for various parts of the
 *  display.  These definitions are used by the routines in "display.c" for
 *  cursor addressing.
 */

#define  x_lastpid	10
#define  y_lastpid	0
#define  x_loadave	33
#define  x_loadave_nompid	15
#define  y_loadave	0
#define  x_procstate	0
#define  y_procstate	1
#define  x_brkdn	15
#define  y_brkdn	1
#define  x_mem		5
#define  y_mem		(y_cpustates+(smart_terminal?n_cpus:1))
#define  x_swap		6
#define  y_swap		(y_mem+1)
#define  y_message	(y_swap+1)
#define  x_header	0
#define  y_header	(y_message+1)
#define  x_idlecursor	0
#define  y_idlecursor	(y_message)
#define  y_procs	(y_header+1)

#define  y_cpustates	2
