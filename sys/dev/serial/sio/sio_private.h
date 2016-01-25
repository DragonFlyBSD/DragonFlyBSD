/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/dev/serial/sio/sio_private.h,v 1.2 2004/09/19 02:05:54 dillon Exp $
 */

#ifdef COM_MULTIPORT
/* checks in flags for multiport and which is multiport "master chip"
 * for a given card
 */
#define	COM_ISMULTIPORT(flags)	((flags) & 0x01)
#define	COM_MPMASTER(flags)	(((flags) >> 8) & 0x0ff)
#define	COM_NOTAST4(flags)	((flags) & 0x04)
#endif /* COM_MULTIPORT */

#define	COM_CONSOLE(flags)	((flags) & 0x10)
#define	COM_FORCECONSOLE(flags)	((flags) & 0x20)
#define	COM_LLCONSOLE(flags)	((flags) & 0x40)
#define	COM_DEBUGGER(flags)	((flags) & 0x80)
#define	COM_LOSESOUTINTS(flags)	((flags) & 0x08)
#define	COM_NOFIFO(flags)		((flags) & 0x02)
#define COM_ST16650A(flags)	((flags) & 0x20000)
#define COM_C_NOPROBE		(0x40000)
#define COM_NOPROBE(flags)	((flags) & COM_C_NOPROBE)
#define COM_C_IIR_TXRDYBUG	(0x80000)
#define COM_IIR_TXRDYBUG(flags)	((flags) & COM_C_IIR_TXRDYBUG)
#define	COM_TI16754(flags)	((flags) & 0x200000)
#define	COM_FIFOSIZE(flags)	(((flags) & 0xff000000) >> 24)

#define	CE_NTYPES			3
#define	CE_RECORD(com, errnum)		(++(com)->delta_error_counts[errnum])

/* types.  XXX - should be elsewhere */
typedef u_int	Port_t;		/* hardware port */
typedef u_char	bool_t;		/* boolean */

/* queue of linear buffers */
struct lbq {
	u_char	*l_head;	/* next char to process */
	u_char	*l_tail;	/* one past the last char to process */
	struct lbq *l_next;	/* next in queue */
	bool_t	l_queued;	/* nonzero if queued */
};

/* com device structure */
struct com_s {
	u_int	flags;		/* Copy isa device flags */
	u_char	state;		/* miscellaneous flag bits */
	bool_t  active_out;	/* nonzero if the callout device is open */
	u_char	cfcr_image;	/* copy of value written to CFCR */
#ifdef COM_ESP
	bool_t	esp;		/* is this unit a hayes esp board? */
#endif
	u_char	extra_state;	/* more flag bits, separate for order trick */
	u_char	fifo_image;	/* copy of value written to FIFO */
	bool_t	hasfifo;	/* nonzero for 16550 UARTs */
	bool_t	st16650a;	/* Is a Startech 16650A or RTS/CTS compat */
	bool_t	loses_outints;	/* nonzero if device loses output interrupts */
	u_char	mcr_image;	/* copy of value written to MCR */
#ifdef COM_MULTIPORT
	bool_t	multiport;	/* is this unit part of a multiport device? */
#endif /* COM_MULTIPORT */
	bool_t	no_irq;		/* nonzero if irq is not attached */
	bool_t  gone;		/* hardware disappeared */
	bool_t	poll;		/* nonzero if polling is required */
	bool_t	poll_output;	/* nonzero if polling for output is required */
	int	unit;		/* unit	number */
	int	dtr_wait;	/* time to hold DTR down on close (* 1/hz) */
	struct callout dtr_ch;
	struct callout busy_ch;
	u_int	tx_fifo_size;
	u_int	wopeners;	/* # processes waiting for DCD in open() */

	/*
	 * The high level of the driver never reads status registers directly
	 * because there would be too many side effects to handle conveniently.
	 * Instead, it reads copies of the registers stored here by the
	 * interrupt handler.
	 */
	u_char	last_modem_status;	/* last MSR read by intr handler */
	u_char	prev_modem_status;	/* last MSR handled by high level */

	u_char	hotchar;	/* ldisc-specific char to be handled ASAP */
	u_char	*ibuf;		/* start of input buffer */
	u_char	*ibufend;	/* end of input buffer */
	u_char	*ibufold;	/* old input buffer, to be freed */
	u_char	*ihighwater;	/* threshold in input buffer */
	u_char	*iptr;		/* next free spot in input buffer */
	int	ibufsize;	/* size of ibuf (not include error bytes) */
	int	ierroff;	/* offset of error bytes in ibuf */

	struct lbq	obufq;	/* head of queue of output buffers */
	struct lbq	obufs[2];	/* output buffers */

	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;

	Port_t	data_port;	/* i/o ports */
#ifdef COM_ESP
	Port_t	esp_port;
#endif
	Port_t	int_id_port;
	Port_t	modem_ctl_port;
	Port_t	line_status_port;
	Port_t	modem_status_port;
	Port_t	intr_ctl_port;	/* Ports of IIR register */

	struct tty	*tp;	/* cross reference */

	/* Initial state. */
	struct termios	it_in;	/* should be in struct tty */
	struct termios	it_out;

	/* Lock state. */
	struct termios	lt_in;	/* should be in struct tty */
	struct termios	lt_out;

	bool_t	do_timestamp;
	bool_t	do_dcd_timestamp;
	struct timeval	timestamp;
	struct timeval	dcd_timestamp;
	struct	pps_state pps;

	u_long	bytes_in;	/* statistics */
	u_long	bytes_out;
	u_int	delta_error_counts[CE_NTYPES];
	u_long	error_counts[CE_NTYPES];

	u_long	rclk;

	struct resource *irqres;
	struct resource *ioportres;
	void *cookie;

	/*
	 * Data area for output buffers.  Someday we should build the output
	 * buffer queue without copying data.
	 */
	u_char	obuf1[256];
	u_char	obuf2[256];
};

#define SET_FLAG(dev, bit) device_set_flags(dev, device_get_flags(dev) | (bit))
#define CLR_FLAG(dev, bit) device_set_flags(dev, device_get_flags(dev) & ~(bit))

extern devclass_t	sio_devclass;

int	sioattach	(device_t dev, int rid, u_long rclk);
int	sioprobe	(device_t dev, int xrid, u_long rclk);
