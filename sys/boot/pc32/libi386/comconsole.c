/*-
 * Copyright (c) 1998 Michael Smith (msmith@freebsd.org)
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
 * $FreeBSD: src/sys/boot/i386/libi386/comconsole.c,v 1.10 2003/09/16 11:24:23 bde Exp $
 * $DragonFly: src/sys/boot/pc32/libi386/comconsole.c,v 1.9 2004/06/27 21:27:36 dillon Exp $
 */

#include <stand.h>
#include <bootstrap.h>
#include <machine/cpufunc.h>
#include <dev/serial/ic_layer/ns16550.h>
#include "libi386.h"

#define COMC_FMT	0x3		/* 8N1 */
#define COMC_TXWAIT	0x40000		/* transmit timeout */
#define COMC_BPS(x)	(115200 / (x))	/* speed to DLAB divisor */

#ifndef	COMPORT
#define COMPORT		0x3f8
#endif
#ifndef	COMSPEED
#define COMSPEED	115200
#endif

static void	comc_probe(struct console *cp);
static int	comc_init(int arg);
static void	comc_putchar(int c);
static int	comc_getchar(void);
static int	comc_ischar(void);

static int	comc_started;

struct console comconsole = {
    "comconsole",
    "serial port",
    0,
    comc_probe,
    comc_init,
    comc_putchar,
    comc_getchar,
    comc_ischar
};

/*
 * Probe for a comconsole.  If the comport is not mapped at boot time (which
 * is often true on laptops), don't try to access it.  If we can't clear 
 * the input fifo, don't try to access it later.
 *
 * Normally the stage-2 bootloader will do this detection and hand us
 * appropriate flags, but some bootloaders (pxe, cdboot) load the loader
 * directly so we have to check again here.
 */
static void
comc_probe(struct console *cp)
{
    int i;

    if (inb(COMPORT + com_lsr) == 0xFF)
	return;
    for (i = 255; i >= 0; --i) {
	if (comc_ischar() == 0)
	    break;
        inb(COMPORT + com_data);
    }
    if (i < 0)
	return;
    cp->c_flags |= (C_PRESENTIN | C_PRESENTOUT);
}

static int
comc_init(int arg)
{
    if (comc_started && arg == 0)
	return 0;
    comc_started = 1;

    outb(COMPORT + com_cfcr, CFCR_DLAB | COMC_FMT);
    outb(COMPORT + com_dlbl, COMC_BPS(COMSPEED) & 0xff);
    outb(COMPORT + com_dlbh, COMC_BPS(COMSPEED) >> 8);
    outb(COMPORT + com_cfcr, COMC_FMT);
    outb(COMPORT + com_mcr, MCR_RTS | MCR_DTR);

#if 0
    /*
     * Enable the FIFO so the serial port output in dual console mode doesn't
     * interfere so much with the disk twiddle.
     *
     * DISABLED - apparently many new laptops implement only the base 8250,
     * writing to this port can crash them.
     */
    outb(COMPORT + com_fifo, FIFO_ENABLE);
#endif

    /*
     * Give the serial port a little time to settle after asserting RTS and
     * DTR, then drain any pending garbage.
     */
    delay(1000000 / 10);
    while (comc_ischar())
        inb(COMPORT + com_data);

    return(0);
}

static void
comc_putchar(int c)
{
    int wait;

    for (wait = COMC_TXWAIT; wait > 0; wait--) {
        if (inb(COMPORT + com_lsr) & LSR_TXRDY) {
	    outb(COMPORT + com_data, (u_char)c);
	    break;
	}
    }
}

static int
comc_getchar(void)
{
    return(comc_ischar() ? inb(COMPORT + com_data) : -1);
}

static int
comc_ischar(void)
{
    return(inb(COMPORT + com_lsr) & LSR_RXRDY);
}
