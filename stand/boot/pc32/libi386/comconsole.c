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
#define COMC_DIV2BPS(x)	(115200 / (x))	/* DLAB divisor to speed */

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
static int	comc_getspeed(void);
static int	comc_ischar(void);
static int	comc_parseint(const char *string);
static int	comc_port_set(struct env_var *ev, int flags, const void *value);
static int	comc_setup(int speed, int port);
static int	comc_speed_set(struct env_var *ev, int flags, const void *value);

static int	comc_started;
static int	comc_curspeed;
static int	comc_port = COMPORT;

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
    char intbuf[16];
    char *env;
    int fromboot, i, speed, port;

    if (comc_curspeed == 0) {
	comc_curspeed = COMSPEED;

	env = getenv("comconsole_port");
	if (env != NULL) {
	    port = comc_parseint(env);
	    if (port > 0)
		comc_port = port;
	}

	/* comconsole_boot is loader-only; see main.c. */
	fromboot = getenv("comconsole_boot") != NULL;
	if (fromboot)
	    unsetenv("comconsole_boot");
	if (fromboot && getenv("comconsole_speed") == NULL &&
	    inb(comc_port + com_lsr) != 0xFF)
		comc_curspeed = comc_getspeed();

	env = getenv("comconsole_speed");
	if (env != NULL) {
	    speed = comc_parseint(env);
	    if (speed > 0)
		comc_curspeed = speed;
	}

	sprintf(intbuf, "%d", comc_curspeed);
	unsetenv("comconsole_speed");
	env_setenv("comconsole_speed", EV_VOLATILE, intbuf, comc_speed_set,
	    env_nounset);

	sprintf(intbuf, "%d", comc_port);
	unsetenv("comconsole_port");
	env_setenv("comconsole_port", EV_VOLATILE, intbuf, comc_port_set,
	    env_nounset);
    }

    if (inb(comc_port + com_lsr) == 0xFF)
	return;
    for (i = 255; i >= 0; --i) {
	if (comc_ischar() == 0)
	    break;
        inb(comc_port + com_data);
    }
    if (i < 0)
	return;
    cp->c_flags |= (C_PRESENTIN | C_PRESENTOUT);
}

static int
comc_setup(int speed, int port)
{
    if (inb(port + com_lsr) == 0xFF)
	return (-1);

    comc_curspeed = speed;
    comc_port = port;

    if ((comconsole.c_flags & (C_ACTIVEIN | C_ACTIVEOUT)) == 0)
	return (0);

    outb(comc_port + com_cfcr, CFCR_DLAB | COMC_FMT);
    outb(comc_port + com_dlbl, COMC_BPS(speed) & 0xff);
    outb(comc_port + com_dlbh, COMC_BPS(speed) >> 8);
    outb(comc_port + com_cfcr, COMC_FMT);
    outb(comc_port + com_mcr, MCR_RTS | MCR_DTR);

#if 0
    /*
     * Enable the FIFO so the serial port output in dual console mode doesn't
     * interfere so much with the disk twiddle.
     *
     * DISABLED - apparently many new laptops implement only the base 8250,
     * writing to this port can crash them.
     */
    outb(comc_port + com_fifo, FIFO_ENABLE);
#endif

    return (0);
}

static int
comc_init(int arg)
{
    if (comc_started && arg == 0)
	return 0;
    comc_started = 1;

    comc_setup(comc_curspeed, comc_port);

    /*
     * Give the serial port a little time to settle after asserting RTS and
     * DTR, then drain any pending garbage.
     */
    delay(1000000 / 10);
    while (comc_ischar())
        inb(comc_port + com_data);

    return(0);
}

static void
comc_putchar(int c)
{
    int wait;

    for (wait = COMC_TXWAIT; wait > 0; wait--) {
        if (inb(comc_port + com_lsr) & LSR_TXRDY) {
	    outb(comc_port + com_data, (u_char)c);
	    break;
	}
    }
}

static int
comc_getchar(void)
{
    return(comc_ischar() ? inb(comc_port + com_data) : -1);
}

static int
comc_ischar(void)
{
    return(inb(comc_port + com_lsr) & LSR_RXRDY);
}

static int
comc_speed_set(struct env_var *ev, int flags, const void *value)
{
    int speed;

    if (value == NULL || (speed = comc_parseint(value)) <= 0) {
	printf("Invalid speed\n");
	return (CMD_ERROR);
    }

    if (comc_curspeed != speed)
	comc_setup(speed, comc_port);

    env_setenv(ev->ev_name, flags | EV_NOHOOK, value, NULL, NULL);

    return (CMD_OK);
}

static int
comc_port_set(struct env_var *ev, int flags, const void *value)
{
    int port;

    if (value == NULL || (port = comc_parseint(value)) <= 0) {
	printf("Invalid port\n");
	return (CMD_ERROR);
    }

    if (comc_port != port && comc_setup(comc_curspeed, port) != 0) {
	printf("Invalid port\n");
	return (CMD_ERROR);
    }

    env_setenv(ev->ev_name, flags | EV_NOHOOK, value, NULL, NULL);

    return (CMD_OK);
}

static int
comc_parseint(const char *string)
{
    char *p;
    int speed;

    speed = strtol(string, &p, 0);
    if (p == string || *p != '\0' || speed <= 0)
	return (-1);

    return (speed);
}

static int
comc_getspeed(void)
{
    u_int divisor;
    u_char dlbh;
    u_char dlbl;
    u_char cfcr;

    cfcr = inb(comc_port + com_cfcr);
    outb(comc_port + com_cfcr, CFCR_DLAB | cfcr);

    dlbl = inb(comc_port + com_dlbl);
    dlbh = inb(comc_port + com_dlbh);

    outb(comc_port + com_cfcr, cfcr);

    divisor = dlbh << 8 | dlbl;

    if (divisor == 0 || divisor == 0xffff)
	return (COMSPEED);
    return (COMC_DIV2BPS(divisor));
}
