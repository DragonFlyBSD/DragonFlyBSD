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
 */

#include <sys/cdefs.h>
#include <stand.h>
#include <bootstrap.h>
#include <machine/cpufunc.h>
#if 0 /* PCI */
#include <bus/pci/pcireg.h>
#endif
#include <ns16550.h>
#include "libi386.h"

#define COMC_FMT	0x3				/* 8N1 */
#define COMC_TXWAIT	0x40000				/* transmit timeout */
#define COMC_BPS(x)     (x ? (115200 / (x)) : 1)	/* speed to DLAB divisor */
#define COMC_DIV2BPS(x) (x ? (115200 / (x)) : 1)	/* DLAB divisor to speed */

#ifndef	COMPORT
#define COMPORT		0x3f8
#endif

#ifndef	COMSPEED
#define COMSPEED	115200
#endif

static int RX_TRY_COUNT = 1000000;

static void	comc_probe(struct console *cp);
static int	comc_init(int arg);
static void	comc_putchar(int c);
static int	comc_getchar(void);
static int	comc_getspeed(void);
static int	comc_ischar(void);
static int	comc_parseint(const char *string);
#if 0 /* PCI */
static uint32_t comc_parse_pcidev(const char *string);
static int	comc_pcidev_set(struct env_var *ev, int flags,
		    const void *value);
static int	comc_pcidev_handle(uint32_t locator);
#endif
static int	comc_port_set(struct env_var *ev, int flags,
		    const void *value);
static void	comc_setup(int speed, int port);
static int	comc_speed_set(struct env_var *ev, int flags,
		    const void *value);

static int	comc_curspeed = 0;
static int	comc_port = COMPORT;
#if 0 /* PCI */
static uint32_t	comc_locator;
#endif

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

static void
comc_probe(struct console *cp)
{
    char intbuf[16];
    char *cons, *env;
    int speed, port;
#if 0 /* PCI */
    uint32_t locator;
#endif
    if (comc_curspeed == 0) {
	comc_curspeed = COMSPEED;
	/*
	 * Assume that the speed was set by an earlier boot loader if
	 * comconsole is already the preferred console.
	 */
	cons = getenv("console");
	if ((cons != NULL && strcmp(cons, comconsole.c_name) == 0) ||
	    getenv("boot_multicons") != NULL) {
		comc_curspeed = comc_getspeed();
	}

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

	env = getenv("comconsole_port");
	if (env != NULL) {
	    port = comc_parseint(env);
	    if (port > 0)
		comc_port = port;
	}

	sprintf(intbuf, "0x%x", comc_port);
	unsetenv("comconsole_port");
	env_setenv("comconsole_port", EV_VOLATILE, intbuf, comc_port_set,
	    env_nounset);

/*
	env = getenv("comconsole_pcidev");
	if (env != NULL) {
	    locator = comc_parse_pcidev(env);
	    if (locator != 0)
		    comc_pcidev_handle(locator);
	}

	unsetenv("comconsole_pcidev");
	env_setenv("comconsole_pcidev", EV_VOLATILE, env, comc_pcidev_set,
	    env_nounset);
*/
    }
    comc_setup(comc_curspeed, comc_port);
}

static int
comc_init(int arg)
{
    comc_setup(comc_curspeed, comc_port);

    if ((comconsole.c_flags & (C_PRESENTIN | C_PRESENTOUT)) ==
	(C_PRESENTIN | C_PRESENTOUT))
	return (CMD_OK);
    return (CMD_ERROR);
}

static void
comc_putchar(int c)
{
    int wait;

    for (wait = COMC_TXWAIT; wait > 0; wait--)
        if (inb(comc_port + com_lsr) & LSR_TXRDY) {
	    outb(comc_port + com_data, (u_char)c);
	    break;
	}
}

static int
comc_getchar(void)
{
    return (comc_ischar() ? inb(comc_port + com_data) : -1);
}

static int
comc_ischar(void)
{
    return (inb(comc_port + com_lsr) & LSR_RXRDY);
}

/* should be moved to one of the common files */
static void
sleep(int timeout) {
    time_t	when, otime, ntime;

    otime = time(NULL);
    when = otime + timeout;	/* when to boot */
    for (;;) {
	ntime = time(NULL);
	if (ntime >= when)
	    break;
        otime = ntime;
    }
}

static int
comc_speed_set(struct env_var *ev, int flags, const void *value)
{
    char intbuf[64];
    int speed;
    int tries = 0;

    if ((comconsole.c_flags & (C_ACTIVEIN | C_ACTIVEOUT)) != (C_ACTIVEIN | C_ACTIVEOUT)) {
	printf("Com 0x%x Not Active\n", comc_port);
	return (CMD_ERROR);
    }

    if (value == NULL || (speed = comc_parseint(value)) <= 0 || 115200 < speed || speed < 75) {
	printf("Invalid speed\n");
	return (CMD_ERROR);
    }

    if (comc_curspeed != speed) {
        printf("Com 0x%x: changing speed from %d to %d baud in 5 seconds, "
               "change your terminal to match!\n\a",
               comc_port, comc_curspeed, speed);
        sleep(5);

	outb(comc_port + com_cfcr, LCR_DLAB);
	outb(comc_port + com_dlbl, COMC_BPS(speed) & 0xff);
	outb(comc_port + com_dlbh, COMC_BPS(speed) >> 8);
	outb(comc_port + com_cfcr, LCR_8BITS);
        outb(comc_port + com_mcr, MCR_RTS | MCR_DTR);
	do
	    inb(comc_port + com_data);
	while (inb(comc_port + com_lsr) & LSR_RXRDY && ++tries < RX_TRY_COUNT);

	if (tries < RX_TRY_COUNT) {
	    comc_curspeed = speed;
	    unsetenv("hw.uart.console");
	    sprintf(intbuf, "io:0x%x,br:%d", comc_port, comc_curspeed);
	    env_setenv("hw.uart.console", EV_VOLATILE, intbuf, NULL, NULL);
	} else {
	    printf("Com 0x%x: Speed change failed\n", comc_port);
	    return (CMD_ERROR);
	    speed = comc_curspeed;
	}
    }
    sprintf(intbuf, "%d", speed);
    env_setenv(ev->ev_name, flags | EV_NOHOOK, intbuf, NULL, NULL);

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

    if (comc_port != port)
	comc_setup(comc_curspeed, port);

    env_setenv(ev->ev_name, flags | EV_NOHOOK, value, NULL, NULL);

    return (CMD_OK);
}

/*
 * Input: bus:dev:func[:bar]. If bar is not specified, it is 0x10.
 * Output: bar[24:16] bus[15:8] dev[7:3] func[2:0]
 */
#if 0 /* PCI */
static uint32_t
comc_parse_pcidev(const char *string)
{
#ifdef NO_PCI
    return (0);
#else
    char *p, *p1;
    uint8_t bus, dev, func, bar;
    uint32_t locator;
    int pres;

    pres = strtol(string, &p, 0);
    if (p == string || *p != ':' || pres < 0 )
            return (0);
    bus = pres;
    p1 = ++p;

    pres = strtol(p1, &p, 0);
    if (p == string || *p != ':' || pres < 0 )
            return (0);
    dev = pres;
    p1 = ++p;

    pres = strtol(p1, &p, 0);
    if (p == string || (*p != ':' && *p != '\0') || pres < 0 )
            return (0);
    func = pres;

    if (*p == ':') {
        p1 = ++p;
        pres = strtol(p1, &p, 0);
        if (p == string || *p != '\0' || pres <= 0 )
                return (0);
        bar = pres;
    } else
        bar = 0x10;

    locator = (bar << 16) | biospci_locator(bus, dev, func);
    return (locator);
#endif
}

static int
comc_pcidev_handle(uint32_t locator)
{
#ifdef NO_PCI
    return (CMD_ERROR);
#else
    char intbuf[64];
    uint32_t port;

    if (biospci_read_config(locator & 0xffff,
        (locator & 0xff0000) >> 16, BIOSPCI_32BITS, &port) == -1) {
            printf("Cannot read bar at 0x%x\n", locator);
            return (CMD_ERROR);
    }

    /*
     * biospci_read_config() sets port == 0xffffffff if the pcidev
     * isn't found on the bus.  Check for 0xffffffff and return to not
     * panic in BTX.
     */
    if (port == 0xffffffff) {
        printf("Cannot find specified pcidev\n");
        return (CMD_ERROR);
    }
    if (!PCI_BAR_IO(port)) {
        printf("Memory bar at 0x%x\n", locator);
        return (CMD_ERROR);
    }
    port &= PCIM_BAR_IO_BASE;

    sprintf(intbuf, "%d", port);
    unsetenv("comconsole_port");
    env_setenv("comconsole_port", EV_VOLATILE, intbuf,
           comc_port_set, env_nounset);

    comc_setup(comc_curspeed, port);
    comc_locator = locator;

    return (CMD_OK);
#endif
}

static int
comc_pcidev_set(struct env_var *ev, int flags, const void *value)
{
    uint32_t locator;
    int error;

    if (value == NULL || (locator = comc_parse_pcidev(value)) <= 0) {
        printf("Invalid pcidev\n");
        return (CMD_ERROR);
    }
    if ((comconsole.c_flags & (C_ACTIVEIN | C_ACTIVEOUT)) != 0 &&
        comc_locator != locator) {
            error = comc_pcidev_handle(locator);
            if (error != CMD_OK)
                return (error);
    }
    env_setenv(ev->ev_name, flags | EV_NOHOOK, value, NULL, NULL);
    return (CMD_OK);
}
#endif

static void
comc_setup(int speed, int port)
{
    char intbuf[64];
    int tries;

    unsetenv("hw.uart.console");
    comc_curspeed = speed;
    comc_port = port;
    if ((comconsole.c_flags & (C_ACTIVEIN | C_ACTIVEOUT)) == 0)
	return;

    outb(comc_port + com_cfcr, CFCR_DLAB | COMC_FMT);
    outb(comc_port + com_dlbl, COMC_BPS(speed) & 0xff);
    outb(comc_port + com_dlbh, COMC_BPS(speed) >> 8);
    outb(comc_port + com_cfcr, COMC_FMT);
    outb(comc_port + com_mcr, MCR_RTS | MCR_DTR);
    outb(comc_port + com_fifo, FIFO_ENABLE | FIFO_RCV_RST | FIFO_XMT_RST | FIFO_DMA_MODE);

    tries = 0;
    do
        inb(comc_port + com_data);
    while (inb(comc_port + com_lsr) & LSR_RXRDY && ++tries < RX_TRY_COUNT);

    if (tries < RX_TRY_COUNT) {
	comconsole.c_flags |= (C_PRESENTIN | C_PRESENTOUT);
	sprintf(intbuf, "io:0x%x,br:%d", comc_port, comc_curspeed);
	env_setenv("hw.uart.console", EV_VOLATILE, intbuf, NULL, NULL);
    } else
	comconsole.c_flags &= ~(C_PRESENTIN | C_PRESENTOUT);
}

static int
comc_parseint(const char *speedstr)
{
    char *p;
    int speed;

    speed = strtol(speedstr, &p, 0);
    if (p == speedstr || *p != '\0' || speed <= 0)
	return (-1);

    return (speed);
}

static int
comc_getspeed(void)
{
    u_int	divisor;
    u_char	dlbh;
    u_char	dlbl;
    u_char	cfcr;

    cfcr = inb(comc_port + com_cfcr);
    outb(comc_port + com_cfcr, CFCR_DLAB | cfcr);

    dlbl = inb(comc_port + com_dlbl);
    dlbh = inb(comc_port + com_dlbh);

    outb(comc_port + com_cfcr, cfcr);

    divisor = dlbh << 8 | dlbl;

    /* XXX there should be more sanity checking. */
    if (divisor == 0)
        return (COMSPEED);
    return (COMC_DIV2BPS(divisor));
}

int
isa_inb(int port)
{
    u_char      data;

    if (__builtin_constant_p(port) &&
        (((port) & 0xffff) < 0x100) &&
        ((port) < 0x10000)) {
        __asm __volatile("inb %1,%0" : "=a" (data) : "id" ((u_short)(port)));
    } else {
        __asm __volatile("inb %%dx,%0" : "=a" (data) : "d" (port));
    }
    return(data);
}

void
isa_outb(int port, int value)
{
    u_char      al = value;

    if (__builtin_constant_p(port) &&
        (((port) & 0xffff) < 0x100) &&
        ((port) < 0x10000)) {
        __asm __volatile("outb %0,%1" : : "a" (al), "id" ((u_short)(port)));
    } else {
        __asm __volatile("outb %0,%%dx" : : "a" (al), "d" (port));
    }
}
