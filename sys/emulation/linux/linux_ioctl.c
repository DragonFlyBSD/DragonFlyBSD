/*
 * Copyright (c) 1994-1995 Søren Schmidt
 * Copyright (c) 2004 Simon 'corecode' Schubert
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/compat/linux/linux_ioctl.c,v 1.55.2.11 2003/05/01 20:16:09 anholt Exp $
 * $DragonFly: src/sys/emulation/linux/linux_ioctl.c,v 1.25 2008/03/07 11:34:19 sephe Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/cdio.h>
#include <sys/consio.h>
#include <sys/ctype.h>
#include <sys/diskslice.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#include <sys/kbio.h>
#include <sys/kernel.h>
#include <sys/linker_set.h>
#include <sys/malloc.h>
#include <sys/mapped_ioctl.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/soundcard.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#include <sys/file2.h>
#include <sys/mplock2.h>

#include <arch_linux/linux.h>
#include <arch_linux/linux_proto.h>

#include "linux_ioctl.h"
#include "linux_mib.h"
#include "linux_util.h"


static int
linux_ioctl_BLKGETSIZE32(struct file *fp, u_long cmd, u_long ocmd,
			 caddr_t data, struct ucred *cred)
{
	struct partinfo dpart;
	u_int32_t value;
	int error;

	error = fo_ioctl(fp, DIOCGPART, (caddr_t)&dpart, cred, NULL);
	if (error)
		return (error);
	value = dpart.media_blocks;	/* 64->32 */
	bcopy(&value, data, sizeof(value));
	return (0);
}


/*
 * termio related ioctls
 */

struct linux_termio {
	unsigned short c_iflag;
	unsigned short c_oflag;
	unsigned short c_cflag;
	unsigned short c_lflag;
	unsigned char c_line;
	unsigned char c_cc[LINUX_NCC];
};

struct linux_termios {
	unsigned int c_iflag;
	unsigned int c_oflag;
	unsigned int c_cflag;
	unsigned int c_lflag;
	unsigned char c_line;
	unsigned char c_cc[LINUX_NCCS];
};

struct linux_winsize {
	unsigned short ws_row, ws_col;
	unsigned short ws_xpixel, ws_ypixel;
};

static struct speedtab sptab[] = {
	{ B0, LINUX_B0 }, { B50, LINUX_B50 },
	{ B75, LINUX_B75 }, { B110, LINUX_B110 },
	{ B134, LINUX_B134 }, { B150, LINUX_B150 },
	{ B200, LINUX_B200 }, { B300, LINUX_B300 },
	{ B600, LINUX_B600 }, { B1200, LINUX_B1200 },
	{ B1800, LINUX_B1800 }, { B2400, LINUX_B2400 },
	{ B4800, LINUX_B4800 }, { B9600, LINUX_B9600 },
	{ B19200, LINUX_B19200 }, { B38400, LINUX_B38400 },
	{ B57600, LINUX_B57600 }, { B115200, LINUX_B115200 },
	{-1, -1 }
};

struct linux_serial_struct {
	int	type;
	int	line;
	int	port;
	int	irq;
	int	flags;
	int	xmit_fifo_size;
	int	custom_divisor;
	int	baud_base;
	unsigned short close_delay;
	char	reserved_char[2];
	int	hub6;
	unsigned short closing_wait;
	unsigned short closing_wait2;
	int	reserved[4];
};

static int
linux_to_bsd_speed(int code, struct speedtab *table)
{
	for ( ; table->sp_code != -1; table++)
		if (table->sp_code == code)
			return (table->sp_speed);
	return -1;
}

static int
bsd_to_linux_speed(int speed, struct speedtab *table)
{
	for ( ; table->sp_speed != -1; table++)
		if (table->sp_speed == speed)
			return (table->sp_code);
	return -1;
}

static void
bsd_to_linux_termios(struct termios *bios, struct linux_termios *lios)
{
	int i;

#ifdef DEBUG
	if (ldebug(ioctl)) {
		kprintf("LINUX: BSD termios structure (input):\n");
		kprintf("i=%08x o=%08x c=%08x l=%08x ispeed=%d ospeed=%d\n",
		    bios->c_iflag, bios->c_oflag, bios->c_cflag, bios->c_lflag,
		    bios->c_ispeed, bios->c_ospeed);
		kprintf("c_cc ");
		for (i=0; i<NCCS; i++)
			kprintf("%02x ", bios->c_cc[i]);
		kprintf("\n");
	}
#endif

	lios->c_iflag = 0;
	if (bios->c_iflag & IGNBRK)
		lios->c_iflag |= LINUX_IGNBRK;
	if (bios->c_iflag & BRKINT)
		lios->c_iflag |= LINUX_BRKINT;
	if (bios->c_iflag & IGNPAR)
		lios->c_iflag |= LINUX_IGNPAR;
	if (bios->c_iflag & PARMRK)
		lios->c_iflag |= LINUX_PARMRK;
	if (bios->c_iflag & INPCK)
		lios->c_iflag |= LINUX_INPCK;
	if (bios->c_iflag & ISTRIP)
		lios->c_iflag |= LINUX_ISTRIP;
	if (bios->c_iflag & INLCR)
		lios->c_iflag |= LINUX_INLCR;
	if (bios->c_iflag & IGNCR)
		lios->c_iflag |= LINUX_IGNCR;
	if (bios->c_iflag & ICRNL)
		lios->c_iflag |= LINUX_ICRNL;
	if (bios->c_iflag & IXON)
		lios->c_iflag |= LINUX_IXON;
	if (bios->c_iflag & IXANY)
		lios->c_iflag |= LINUX_IXANY;
	if (bios->c_iflag & IXOFF)
		lios->c_iflag |= LINUX_IXOFF;
	if (bios->c_iflag & IMAXBEL)
		lios->c_iflag |= LINUX_IMAXBEL;

	lios->c_oflag = 0;
	if (bios->c_oflag & OPOST)
		lios->c_oflag |= LINUX_OPOST;
	if (bios->c_oflag & ONLCR)
		lios->c_oflag |= LINUX_ONLCR;
	if (bios->c_oflag & OXTABS)
		lios->c_oflag |= LINUX_XTABS;

	lios->c_cflag = bsd_to_linux_speed(bios->c_ispeed, sptab);
	lios->c_cflag |= (bios->c_cflag & CSIZE) >> 4;
	if (bios->c_cflag & CSTOPB)
		lios->c_cflag |= LINUX_CSTOPB;
	if (bios->c_cflag & CREAD)
		lios->c_cflag |= LINUX_CREAD;
	if (bios->c_cflag & PARENB)
		lios->c_cflag |= LINUX_PARENB;
	if (bios->c_cflag & PARODD)
		lios->c_cflag |= LINUX_PARODD;
	if (bios->c_cflag & HUPCL)
		lios->c_cflag |= LINUX_HUPCL;
	if (bios->c_cflag & CLOCAL)
		lios->c_cflag |= LINUX_CLOCAL;
	if (bios->c_cflag & CRTSCTS)
		lios->c_cflag |= LINUX_CRTSCTS;

	lios->c_lflag = 0;
	if (bios->c_lflag & ISIG)
		lios->c_lflag |= LINUX_ISIG;
	if (bios->c_lflag & ICANON)
		lios->c_lflag |= LINUX_ICANON;
	if (bios->c_lflag & ECHO)
		lios->c_lflag |= LINUX_ECHO;
	if (bios->c_lflag & ECHOE)
		lios->c_lflag |= LINUX_ECHOE;
	if (bios->c_lflag & ECHOK)
		lios->c_lflag |= LINUX_ECHOK;
	if (bios->c_lflag & ECHONL)
		lios->c_lflag |= LINUX_ECHONL;
	if (bios->c_lflag & NOFLSH)
		lios->c_lflag |= LINUX_NOFLSH;
	if (bios->c_lflag & TOSTOP)
		lios->c_lflag |= LINUX_TOSTOP;
	if (bios->c_lflag & ECHOCTL)
		lios->c_lflag |= LINUX_ECHOCTL;
	if (bios->c_lflag & ECHOPRT)
		lios->c_lflag |= LINUX_ECHOPRT;
	if (bios->c_lflag & ECHOKE)
		lios->c_lflag |= LINUX_ECHOKE;
	if (bios->c_lflag & FLUSHO)
		lios->c_lflag |= LINUX_FLUSHO;
	if (bios->c_lflag & PENDIN)
		lios->c_lflag |= LINUX_PENDIN;
	if (bios->c_lflag & IEXTEN)
		lios->c_lflag |= LINUX_IEXTEN;

	for (i=0; i<LINUX_NCCS; i++)
		lios->c_cc[i] = LINUX_POSIX_VDISABLE;
	lios->c_cc[LINUX_VINTR] = bios->c_cc[VINTR];
	lios->c_cc[LINUX_VQUIT] = bios->c_cc[VQUIT];
	lios->c_cc[LINUX_VERASE] = bios->c_cc[VERASE];
	lios->c_cc[LINUX_VKILL] = bios->c_cc[VKILL];
	lios->c_cc[LINUX_VEOF] = bios->c_cc[VEOF];
	lios->c_cc[LINUX_VEOL] = bios->c_cc[VEOL];
	lios->c_cc[LINUX_VMIN] = bios->c_cc[VMIN];
	lios->c_cc[LINUX_VTIME] = bios->c_cc[VTIME];
	lios->c_cc[LINUX_VEOL2] = bios->c_cc[VEOL2];
	lios->c_cc[LINUX_VSUSP] = bios->c_cc[VSUSP];
	lios->c_cc[LINUX_VSTART] = bios->c_cc[VSTART];
	lios->c_cc[LINUX_VSTOP] = bios->c_cc[VSTOP];
	lios->c_cc[LINUX_VREPRINT] = bios->c_cc[VREPRINT];
	lios->c_cc[LINUX_VDISCARD] = bios->c_cc[VDISCARD];
	lios->c_cc[LINUX_VWERASE] = bios->c_cc[VWERASE];
	lios->c_cc[LINUX_VLNEXT] = bios->c_cc[VLNEXT];

	for (i=0; i<LINUX_NCCS; i++) {
		 if (i != LINUX_VMIN && i != LINUX_VTIME &&
		    lios->c_cc[i] == _POSIX_VDISABLE)
			lios->c_cc[i] = LINUX_POSIX_VDISABLE;
	}
	lios->c_line = 0;

#ifdef DEBUG
	if (ldebug(ioctl)) {
		kprintf("LINUX: LINUX termios structure (output):\n");
		kprintf("i=%08x o=%08x c=%08x l=%08x line=%d\n",
		    lios->c_iflag, lios->c_oflag, lios->c_cflag,
		    lios->c_lflag, (int)lios->c_line);
		kprintf("c_cc ");
		for (i=0; i<LINUX_NCCS; i++) 
			kprintf("%02x ", lios->c_cc[i]);
		kprintf("\n");
	}
#endif
}

static void
linux_to_bsd_termios(struct linux_termios *lios, struct termios *bios)
{
	int i;

#ifdef DEBUG
	if (ldebug(ioctl)) {
		kprintf("LINUX: LINUX termios structure (input):\n");
		kprintf("i=%08x o=%08x c=%08x l=%08x line=%d\n", 
		    lios->c_iflag, lios->c_oflag, lios->c_cflag,
		    lios->c_lflag, (int)lios->c_line);
		kprintf("c_cc ");
		for (i=0; i<LINUX_NCCS; i++)
			kprintf("%02x ", lios->c_cc[i]);
		kprintf("\n");
	}
#endif

	bios->c_iflag = 0;
	if (lios->c_iflag & LINUX_IGNBRK)
		bios->c_iflag |= IGNBRK;
	if (lios->c_iflag & LINUX_BRKINT)
		bios->c_iflag |= BRKINT;
	if (lios->c_iflag & LINUX_IGNPAR)
		bios->c_iflag |= IGNPAR;
	if (lios->c_iflag & LINUX_PARMRK)
		bios->c_iflag |= PARMRK;
	if (lios->c_iflag & LINUX_INPCK)
		bios->c_iflag |= INPCK;
	if (lios->c_iflag & LINUX_ISTRIP)
		bios->c_iflag |= ISTRIP;
	if (lios->c_iflag & LINUX_INLCR)
		bios->c_iflag |= INLCR;
	if (lios->c_iflag & LINUX_IGNCR)
		bios->c_iflag |= IGNCR;
	if (lios->c_iflag & LINUX_ICRNL)
		bios->c_iflag |= ICRNL;
	if (lios->c_iflag & LINUX_IXON)
		bios->c_iflag |= IXON;
	if (lios->c_iflag & LINUX_IXANY)
		bios->c_iflag |= IXANY;
	if (lios->c_iflag & LINUX_IXOFF)
		bios->c_iflag |= IXOFF;
	if (lios->c_iflag & LINUX_IMAXBEL)
		bios->c_iflag |= IMAXBEL;

	bios->c_oflag = 0;
	if (lios->c_oflag & LINUX_OPOST)
		bios->c_oflag |= OPOST;
	if (lios->c_oflag & LINUX_ONLCR)
		bios->c_oflag |= ONLCR;
	if (lios->c_oflag & LINUX_XTABS)
		bios->c_oflag |= OXTABS;

	bios->c_cflag = (lios->c_cflag & LINUX_CSIZE) << 4;
	if (lios->c_cflag & LINUX_CSTOPB)
		bios->c_cflag |= CSTOPB;
	if (lios->c_cflag & LINUX_CREAD)
		bios->c_cflag |= CREAD;
	if (lios->c_cflag & LINUX_PARENB)
		bios->c_cflag |= PARENB;
	if (lios->c_cflag & LINUX_PARODD)
		bios->c_cflag |= PARODD;
	if (lios->c_cflag & LINUX_HUPCL)
		bios->c_cflag |= HUPCL;
	if (lios->c_cflag & LINUX_CLOCAL)
		bios->c_cflag |= CLOCAL;
	if (lios->c_cflag & LINUX_CRTSCTS)
		bios->c_cflag |= CRTSCTS;

	bios->c_lflag = 0;
	if (lios->c_lflag & LINUX_ISIG)
		bios->c_lflag |= ISIG;
	if (lios->c_lflag & LINUX_ICANON)
		bios->c_lflag |= ICANON;
	if (lios->c_lflag & LINUX_ECHO)
		bios->c_lflag |= ECHO;
	if (lios->c_lflag & LINUX_ECHOE)
		bios->c_lflag |= ECHOE;
	if (lios->c_lflag & LINUX_ECHOK)
		bios->c_lflag |= ECHOK;
	if (lios->c_lflag & LINUX_ECHONL)
		bios->c_lflag |= ECHONL;
	if (lios->c_lflag & LINUX_NOFLSH)
		bios->c_lflag |= NOFLSH;
	if (lios->c_lflag & LINUX_TOSTOP)
		bios->c_lflag |= TOSTOP;
	if (lios->c_lflag & LINUX_ECHOCTL)
		bios->c_lflag |= ECHOCTL;
	if (lios->c_lflag & LINUX_ECHOPRT)
		bios->c_lflag |= ECHOPRT;
	if (lios->c_lflag & LINUX_ECHOKE)
		bios->c_lflag |= ECHOKE;
	if (lios->c_lflag & LINUX_FLUSHO)
		bios->c_lflag |= FLUSHO;
	if (lios->c_lflag & LINUX_PENDIN)
		bios->c_lflag |= PENDIN;
	if (lios->c_lflag & LINUX_IEXTEN)
		bios->c_lflag |= IEXTEN;

	for (i=0; i<NCCS; i++)
		bios->c_cc[i] = _POSIX_VDISABLE;
	bios->c_cc[VINTR] = lios->c_cc[LINUX_VINTR];
	bios->c_cc[VQUIT] = lios->c_cc[LINUX_VQUIT];
	bios->c_cc[VERASE] = lios->c_cc[LINUX_VERASE];
	bios->c_cc[VKILL] = lios->c_cc[LINUX_VKILL];
	bios->c_cc[VEOF] = lios->c_cc[LINUX_VEOF];
	bios->c_cc[VEOL] = lios->c_cc[LINUX_VEOL];
	bios->c_cc[VMIN] = lios->c_cc[LINUX_VMIN];
	bios->c_cc[VTIME] = lios->c_cc[LINUX_VTIME];
	bios->c_cc[VEOL2] = lios->c_cc[LINUX_VEOL2];
	bios->c_cc[VSUSP] = lios->c_cc[LINUX_VSUSP];
	bios->c_cc[VSTART] = lios->c_cc[LINUX_VSTART];
	bios->c_cc[VSTOP] = lios->c_cc[LINUX_VSTOP];
	bios->c_cc[VREPRINT] = lios->c_cc[LINUX_VREPRINT];
	bios->c_cc[VDISCARD] = lios->c_cc[LINUX_VDISCARD];
	bios->c_cc[VWERASE] = lios->c_cc[LINUX_VWERASE];
	bios->c_cc[VLNEXT] = lios->c_cc[LINUX_VLNEXT];

	for (i=0; i<NCCS; i++) {
		 if (i != VMIN && i != VTIME &&
		    bios->c_cc[i] == LINUX_POSIX_VDISABLE)
			bios->c_cc[i] = _POSIX_VDISABLE;
	}

	bios->c_ispeed = bios->c_ospeed =
	    linux_to_bsd_speed(lios->c_cflag & LINUX_CBAUD, sptab);

#ifdef DEBUG
	if (ldebug(ioctl)) {
		kprintf("LINUX: BSD termios structure (output):\n");
		kprintf("i=%08x o=%08x c=%08x l=%08x ispeed=%d ospeed=%d\n",
		    bios->c_iflag, bios->c_oflag, bios->c_cflag, bios->c_lflag,
		    bios->c_ispeed, bios->c_ospeed);
		kprintf("c_cc ");
		for (i=0; i<NCCS; i++) 
			kprintf("%02x ", bios->c_cc[i]);
		kprintf("\n");
	}
#endif
}

static void
bsd_to_linux_termio(struct termios *bios, struct linux_termio *lio)
{
	struct linux_termios lios;

	bsd_to_linux_termios(bios, &lios);
	lio->c_iflag = lios.c_iflag;
	lio->c_oflag = lios.c_oflag;
	lio->c_cflag = lios.c_cflag;
	lio->c_lflag = lios.c_lflag;
	lio->c_line  = lios.c_line;
	memcpy(lio->c_cc, lios.c_cc, LINUX_NCC);
}

static void
linux_to_bsd_termio(struct linux_termio *lio, struct termios *bios)
{
	struct linux_termios lios;
	int i;

	lios.c_iflag = lio->c_iflag;
	lios.c_oflag = lio->c_oflag;
	lios.c_cflag = lio->c_cflag;
	lios.c_lflag = lio->c_lflag;
	for (i=LINUX_NCC; i<LINUX_NCCS; i++)
		lios.c_cc[i] = LINUX_POSIX_VDISABLE;
	memcpy(lios.c_cc, lio->c_cc, LINUX_NCC);
	linux_to_bsd_termios(&lios, bios);
}

static int
linux_ioctl_TCGETS(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct termios bios;
	struct linux_termios lios;
	int error;

	error = fo_ioctl(fp, TIOCGETA, (caddr_t)&bios, cred, NULL);
	if (error)
		return (error);
	bsd_to_linux_termios(&bios, &lios);
	bcopy(&lios, data, sizeof(lios));
	return (0);
}

static int
linux_ioctl_TCSETS(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct termios bios;
	struct linux_termios lios;

	bcopy(data, &lios, sizeof(lios));
	linux_to_bsd_termios(&lios, &bios);
	return (fo_ioctl(fp, TIOCSETA, (caddr_t)&bios, cred, NULL));
}

static int
linux_ioctl_TCSETSW(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct termios bios;
	struct linux_termios lios;

	bcopy(data, &lios, sizeof(lios));
	linux_to_bsd_termios(&lios, &bios);
	return (fo_ioctl(fp, TIOCSETAW, (caddr_t)&bios, cred, NULL));
}

static int
linux_ioctl_TCSETSF(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct termios bios;
	struct linux_termios lios;

	bcopy(data, &lios, sizeof(lios));
	linux_to_bsd_termios(&lios, &bios);
	return (fo_ioctl(fp, TIOCSETAF, (caddr_t)&bios, cred, NULL));
}

static int
linux_ioctl_TCGETA(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct termios bios;
	struct linux_termio lio;
	int error;

	error = fo_ioctl(fp, TIOCGETA, (caddr_t)&bios, cred, NULL);
	if (error)
		return (error);
	bsd_to_linux_termio(&bios, &lio);
	bcopy(&lio, data, sizeof(lio));
	return (0);
}

static int
linux_ioctl_TCSETA(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct termios bios;
	struct linux_termio lio;

	bcopy(data, &lio, sizeof(lio));
	linux_to_bsd_termio(&lio, &bios);
	return (fo_ioctl(fp, TIOCSETA, (caddr_t)&bios, cred, NULL));
}

static int
linux_ioctl_TCSETAW(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct termios bios;
	struct linux_termio lio;

	bcopy(data, &lio, sizeof(lio));
	linux_to_bsd_termio(&lio, &bios);
	return (fo_ioctl(fp, TIOCSETAW, (caddr_t)&bios, cred, NULL));
}

static int
linux_ioctl_TCSETAF(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct termios bios;
	struct linux_termio lio;

	bcopy(data, &lio, sizeof(lio));
	linux_to_bsd_termio(&lio, &bios);
	return (fo_ioctl(fp, TIOCSETAF, (caddr_t)&bios, cred, NULL));
}

static int
linux_ioctl_TIOCLINUX(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	switch ((u_char)*data) {
	case 11: /* LINUX_TIOCLINUX_KERNMSG */
		return 0;
	default:
		kprintf("Unknown LINUX_TIOCLINUX: %d\n", ((u_char)*data));
		kprintf("cmd = %lu, ocmd = %lu\n", cmd, ocmd);
		return 0;
	}
	return 0;
}

static int
linux_ioctl_TCXONC(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	switch ((u_long)data) {
	case LINUX_TCOOFF:
		cmd = TIOCSTOP;
		break;
	case LINUX_TCOON:
		cmd = TIOCSTART;
		break;
	case LINUX_TCIOFF:
	case LINUX_TCION: {
		struct termios bios;
		int error, c;
		
		error = fo_ioctl(fp, TIOCGETA, (caddr_t)&bios, cred, NULL);
		if (error)
			return (error);
		c = ((u_long)data == LINUX_TCIOFF) ? VSTOP : VSTART;
		c = bios.c_cc[c];
		if (c != _POSIX_VDISABLE) {
			struct uio auio;
			struct iovec aiov;

			aiov.iov_base = (char *)&c;
			aiov.iov_len = sizeof(*bios.c_cc);
			auio.uio_iov = &aiov;
			auio.uio_iovcnt = 1;
			auio.uio_offset = -1;
			auio.uio_resid = sizeof(*bios.c_cc);
			auio.uio_rw = UIO_WRITE;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_td = curthread;

			return (fo_write(fp, &auio, fp->f_cred, 0));
		}

		return (0);
	}
	default:
		return (EINVAL);
	}
	return (fo_ioctl(fp, cmd, 0, cred, NULL));
}

static int
linux_ioctl_TCFLSH(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	switch ((u_long)data) {
	case LINUX_TCIFLUSH:
		*(u_long *)data = FREAD;
		break;
	case LINUX_TCOFLUSH:
		*(u_long *)data = FWRITE;
		break;
	case LINUX_TCIOFLUSH:
		*(u_long *)data = FREAD | FWRITE;
		break;
	default:
		return (EINVAL);
	}
	return (fo_ioctl(fp, TIOCFLUSH, data, cred, NULL));
}

static int
linux_ioctl_TIOCGSERIAL(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct linux_serial_struct lss;

	lss.type = LINUX_PORT_16550A;
	lss.flags = 0;
	lss.close_delay = 0;
	bcopy(&lss, data, sizeof(lss));
	return (0);
}

static int
linux_ioctl_TIOCSSERIAL(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
#if 0
	struct linux_serial_struct lss;

	bcopy(data, &lss, sizeof(lss));
	/* XXX - It really helps to have an implementation that
	 * does nothing. NOT!
	 */
#endif
	return (0);
}

static int
linux_ioctl_TIOCSETD(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	int line;

	switch ((u_long)data) {
	case LINUX_N_TTY:
		line = TTYDISC;
		break;
	case LINUX_N_SLIP:
		line = SLIPDISC;
		break;
	case LINUX_N_PPP:
		line = PPPDISC;
		break;
	default:
		return (EINVAL);
	}
	return (fo_ioctl(fp, TIOCSETD, (caddr_t)&line, cred, NULL));
}

static int
linux_ioctl_TIOCGETD(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	int linux_line, error;
	int bsd_line = TTYDISC;

	error = fo_ioctl(fp, TIOCGETD, (caddr_t)&bsd_line, cred, NULL);
	if (error)
		return (error);
	switch (bsd_line) {
	case TTYDISC:
		linux_line = LINUX_N_TTY;
		break;
	case SLIPDISC:
		linux_line = LINUX_N_SLIP;
		break;
	case PPPDISC:
		linux_line = LINUX_N_PPP;
		break;
	default:
		return (EINVAL);
	}
	bcopy(&linux_line, data, sizeof(int));
	return (0);
}	


/*
 * CDROM related ioctls
 */

struct linux_cdrom_msf
{
	u_char	cdmsf_min0;
	u_char	cdmsf_sec0;
	u_char	cdmsf_frame0;
	u_char	cdmsf_min1;
	u_char	cdmsf_sec1;
	u_char	cdmsf_frame1;
};

struct linux_cdrom_tochdr
{
	u_char	cdth_trk0;
	u_char	cdth_trk1;
};

union linux_cdrom_addr
{
	struct {
		u_char	minute;
		u_char	second;
		u_char	frame;
	} msf;
	int	lba;
};

struct linux_cdrom_tocentry
{
	u_char	cdte_track;     
	u_char	cdte_adr:4;
	u_char	cdte_ctrl:4;
	u_char	cdte_format;    
	union linux_cdrom_addr cdte_addr;
	u_char	cdte_datamode;  
};

struct linux_cdrom_subchnl
{
	u_char	cdsc_format;
	u_char	cdsc_audiostatus;
	u_char	cdsc_adr:4;
	u_char	cdsc_ctrl:4;
	u_char	cdsc_trk;
	u_char	cdsc_ind;
	union linux_cdrom_addr cdsc_absaddr;
	union linux_cdrom_addr cdsc_reladdr;
};

static void
bsd_to_linux_msf_lba(u_char af, union msf_lba *bp, union linux_cdrom_addr *lp)
{
	if (af == CD_LBA_FORMAT)
		lp->lba = bp->lba;
	else {
		lp->msf.minute = bp->msf.minute;
		lp->msf.second = bp->msf.second;
		lp->msf.frame = bp->msf.frame;
	}
}

static void
set_linux_cdrom_addr(union linux_cdrom_addr *addr, int format, int lba)
{
	if (format == LINUX_CDROM_MSF) {
		addr->msf.frame = lba % 75;
		lba /= 75;
		lba += 2;
		addr->msf.second = lba % 60;
		addr->msf.minute = lba / 60;
	} else
		addr->lba = lba;
}

static int
linux_ioctl_CDROMREADTOCHDR(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct ioc_toc_header th;
	struct linux_cdrom_tochdr lth;
	int error;

	error = fo_ioctl(fp, CDIOREADTOCHEADER, (caddr_t)&th, cred, NULL);
	if (error)
		return (error);
	lth.cdth_trk0 = th.starting_track;
	lth.cdth_trk1 = th.ending_track;
	bcopy(&lth, data, sizeof(lth));
	return (0);
}

static int
linux_ioctl_CDROMREADTOCENTRY(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct linux_cdrom_tocentry *ltep = (struct linux_cdrom_tocentry *)data;
	struct ioc_read_toc_single_entry irtse;
	int error;

	irtse.address_format = ltep->cdte_format;
	irtse.track = ltep->cdte_track;
	error = fo_ioctl(fp, CDIOREADTOCENTRY, (caddr_t)&irtse, cred, NULL);
	if (error)
		return (error);

	ltep->cdte_ctrl = irtse.entry.control;
	ltep->cdte_adr = irtse.entry.addr_type;
	bsd_to_linux_msf_lba(irtse.address_format, &irtse.entry.addr,
			     &ltep->cdte_addr);
	return (0);
}	

static int
linux_ioctl_CDROMSUBCHNL(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct linux_cdrom_subchnl *sc = (struct linux_cdrom_subchnl *)data;
	struct ioc_read_subchannel bsdsc;
	struct cd_sub_channel_info *bsdinfo;
	int error;
	caddr_t sg = stackgap_init();

	bsdinfo = stackgap_alloc(&sg, sizeof(struct cd_sub_channel_info));
	bsdsc.address_format = CD_LBA_FORMAT;
	bsdsc.data_format = CD_CURRENT_POSITION;
	bsdsc.track = 0;
	bsdsc.data_len = sizeof(struct cd_sub_channel_info);
	bsdsc.data = bsdinfo;
	error = fo_ioctl(fp, CDIOCREADSUBCHANNEL, (caddr_t)&bsdsc, cred, NULL);
	if (error)
		return (error);
	sc->cdsc_audiostatus = bsdinfo->header.audio_status;
	sc->cdsc_adr = bsdinfo->what.position.addr_type;
	sc->cdsc_ctrl = bsdinfo->what.position.control;
	sc->cdsc_trk = bsdinfo->what.position.track_number;
	sc->cdsc_ind = bsdinfo->what.position.index_number;
	set_linux_cdrom_addr(&sc->cdsc_absaddr, sc->cdsc_format, bsdinfo->what.position.absaddr.lba);
	set_linux_cdrom_addr(&sc->cdsc_reladdr, sc->cdsc_format, bsdinfo->what.position.reladdr.lba);
	return (0);
}


/*
 * Sound related ioctls
 */

static int
linux_ioctl_OSS_GETVERSION(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	int version = linux_get_oss_version(curthread);

	bcopy(&version, data, sizeof(int));
	return (0);
}


/*
 * Console related ioctls
 */

#define ISSIGVALID(sig)		((sig) > 0 && (sig) < NSIG)

static int
linux_ioctl_KDSKBMODE(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	int kbdmode;

	switch ((u_long)data) {
	case LINUX_KBD_RAW:
		kbdmode = K_RAW;
		break;
	case LINUX_KBD_XLATE:
		kbdmode = K_XLATE;
		break;
	case LINUX_KBD_MEDIUMRAW:
		kbdmode = K_RAW;
		break;
	default:
		return (EINVAL);
	}
	return (fo_ioctl(fp, KDSKBMODE, (caddr_t)&kbdmode, cred, NULL));
}

static int
linux_ioctl_VT_SETMODE(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct vt_mode *mode = (struct vt_mode *)data;

	if (!ISSIGVALID(mode->frsig) && ISSIGVALID(mode->acqsig))
		mode->frsig = mode->acqsig;
	return (fo_ioctl(fp, VT_SETMODE, data, cred, NULL));
}


/*
 * Socket related ioctls
 */

/*
 * Criteria for interface name translation
 */
#define IFP_IS_ETH(ifp) (ifp->if_type == IFT_ETHER)

/*
 * Interface function used by linprocfs (at the time of writing). It's not
 * used by the Linuxulator itself.
 */
int
linux_ifname(struct ifnet *ifp, char *buffer, size_t buflen)
{
	struct ifnet *ifscan;
	int ethno;

	/* Short-circuit non ethernet interfaces */
	if (!IFP_IS_ETH(ifp))
		return (strlcpy(buffer, ifp->if_xname, buflen));

	/* Determine the (relative) unit number for ethernet interfaces */
	ethno = 0;
	TAILQ_FOREACH(ifscan, &ifnetlist, if_link) {
		if (ifscan == ifp)
			return (ksnprintf(buffer, buflen, "eth%d", ethno));
		if (IFP_IS_ETH(ifscan))
			ethno++;
	}

	return (0);
}

/*
 * Translate a Linux interface name to a FreeBSD interface name,
 * and return the associated ifnet structure
 * bsdname and lxname need to be least IFNAMSIZ bytes long, but
 * can point to the same buffer.
 */

static struct ifnet *
ifname_linux_to_bsd(const char *lxname, char *bsdname)
{
	struct ifnet *ifp;
	int len, unit;
	char *ep;
	int is_eth, index;

	for (len = 0; len < LINUX_IFNAMSIZ; ++len)
		if (!isalpha(lxname[len]))
			break;
	if (len == 0 || len == LINUX_IFNAMSIZ)
		return (NULL);
	unit = (int)strtoul(lxname + len, &ep, 10);
	if (ep == NULL || ep == lxname + len || ep >= lxname + LINUX_IFNAMSIZ)
		return (NULL);
	index = 0;
	is_eth = (len == 3 && !strncmp(lxname, "eth", len)) ? 1 : 0;
	TAILQ_FOREACH(ifp, &ifnetlist, if_link) {
		/*
		 * Allow Linux programs to use FreeBSD names. Don't presume
		 * we never have an interface named "eth", so don't make
		 * the test optional based on is_eth.
		 */
		if (strncmp(ifp->if_xname, lxname, LINUX_IFNAMSIZ) == 0)
			break;
		if (is_eth && IFP_IS_ETH(ifp) && unit == index++)
			break;
	}
	if (ifp != NULL)
		strlcpy(bsdname, ifp->if_xname, IFNAMSIZ);
	return (ifp);
}

static int
linux_ioctl_SIOCGIFCONF(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct ifconf *ifc = (struct ifconf *)data;
	struct l_ifreq ifr;
	struct ifnet *ifp;
	struct iovec iov;
	struct uio uio;
	int error, ethno;

	/* much easier to use uiomove than keep track ourselves */
	iov.iov_base = ifc->ifc_buf;
	iov.iov_len = ifc->ifc_len;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 0;
	uio.uio_resid = ifc->ifc_len;
	uio.uio_segflg = UIO_USERSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_td = curthread;

	/* Keep track of eth interfaces */
	ethno = 0;

	/* Return all AF_INET addresses of all interfaces */
	ifnet_lock();
	TAILQ_FOREACH(ifp, &ifnetlist, if_link) {
		struct ifaddr_container *ifac, *ifac_mark;
		struct ifaddr_marker mark;
		struct ifaddrhead *head;

		if (uio.uio_resid <= 0)
			break;

		bzero(&ifr, sizeof ifr);
		if (IFP_IS_ETH(ifp))
			ksnprintf(ifr.ifr_name, LINUX_IFNAMSIZ, "eth%d",
			    ethno++);
		else
			strlcpy(ifr.ifr_name, ifp->if_xname, LINUX_IFNAMSIZ);

		/*
		 * Walk the address list
		 *
		 * Add a marker, since uiomove() could block and during that
		 * period the list could be changed.  Inserting the marker to
		 * the header of the list will not cause trouble for the code
		 * assuming that the first element of the list is AF_LINK; the
		 * marker will be moved to the next position w/o blocking.
		 */
		ifa_marker_init(&mark, ifp);
		ifac_mark = &mark.ifac;
		head = &ifp->if_addrheads[mycpuid];

		TAILQ_INSERT_HEAD(head, ifac_mark, ifa_link);
		while ((ifac = TAILQ_NEXT(ifac_mark, ifa_link)) != NULL) {
			struct ifaddr *ifa = ifac->ifa;
			struct sockaddr *sa = ifa->ifa_addr;

			TAILQ_REMOVE(head, ifac_mark, ifa_link);
			TAILQ_INSERT_AFTER(head, ifac, ifac_mark, ifa_link);

			if (uio.uio_resid <= 0)
				break;

			if (sa->sa_family == AF_INET) {
				ifr.ifr_addr.sa_family = LINUX_AF_INET;
				memcpy(ifr.ifr_addr.sa_data, sa->sa_data,
				    sizeof(ifr.ifr_addr.sa_data));

				error = uiomove((caddr_t)&ifr, sizeof ifr,
				    &uio);
				if (error != 0) {
					TAILQ_REMOVE(head, ifac_mark, ifa_link);
					ifnet_unlock();
					return (error);
				}
			}
		}
		TAILQ_REMOVE(head, ifac_mark, ifa_link);
	}
	ifnet_unlock();

	ifc->ifc_len -= uio.uio_resid;

	return (0);
}

static int
linux_ioctl_SIOCGIFFLAGS(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct l_ifreq *ifr = (struct l_ifreq *)data;
	struct ifnet *ifp;
	char ifname[IFNAMSIZ];
	l_short flags;

#if 0
	if (fp->f_type != DTYPE_SOCKET) {
		/* XXX: I doubt this is correct because
		 *      we don't translate the ifname and
		 *      use l_ifreq instead of ifreq
		 */
		return (fo_ioctl(fp, SIOCGIFFLAGS, data, cred, NULL));
	}
#endif

	ifnet_lock();

	ifp = ifname_linux_to_bsd(ifr->ifr_name, ifname);
	if (ifp == NULL) {
		ifnet_unlock();
		return (EINVAL);
	}
	flags = ifp->if_flags;

	ifnet_unlock();

	/* these flags have no Linux equivalent */
	flags &= ~(IFF_SMART|IFF_SIMPLEX| IFF_LINK0|IFF_LINK1|IFF_LINK2);
	/* Linux' multicast flag is in a different bit */
	if (flags & IFF_MULTICAST) {
		flags &= ~IFF_MULTICAST;
		flags |= 0x1000;
	}

	ifr->ifr_flags = flags;
	return (0);
}

#define ARPHRD_ETHER	1
#define ARPHRD_LOOPBACK	772

/* XXX: could implement using native ioctl, so only mapping */
static int
linux_ioctl_SIOCGIFINDEX(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct l_ifreq *ifr = (struct l_ifreq *)data;
	struct ifnet *ifp;
	char ifname[IFNAMSIZ];
	l_int index;

	ifnet_lock();

	ifp = ifname_linux_to_bsd(ifr->ifr_name, ifname);
	if (ifp == NULL) {
		ifnet_unlock();
		return EINVAL;
	}
#if DEBUG
	kprintf("Interface index: %d\n", ifp->if_index);
#endif
	index = ifp->if_index;

	ifnet_unlock();

	return (copyout(&index, &ifr->ifr_ifindex, sizeof(index)));
}

static int
linux_ioctl_SIOCGIFMETRIC(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct l_ifreq *ifr = (struct l_ifreq *)data;
	struct ifnet *ifp;
	char ifname[IFNAMSIZ];
	l_int metric;

	ifnet_lock();

	ifp = ifname_linux_to_bsd(ifr->ifr_name, ifname);
	if (ifp == NULL) {
		ifnet_unlock();
		return EINVAL;
	}
	metric = ifp->if_metric;

	ifnet_unlock();

	return (copyout(&metric, &ifr->ifr_ifmetric, sizeof(metric)));
}

static int
linux_ioctl_SIOGIFHWADDR(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct l_ifreq *ifr = (struct l_ifreq *)data;
	struct ifnet *ifp;
	char ifname[IFNAMSIZ];
	struct sockaddr_dl *sdl;
	struct l_sockaddr lsa;
	struct ifaddr_container *ifac;

	ifnet_lock();

	ifp = ifname_linux_to_bsd(ifr->ifr_name, ifname);
	if (ifp == NULL) {
		ifnet_unlock();
		return EINVAL;
	}

	if (ifp->if_type == IFT_LOOP) {
		ifnet_unlock();
		bzero(&ifr->ifr_hwaddr, sizeof lsa);
		ifr->ifr_hwaddr.sa_family = ARPHRD_LOOPBACK;
		return (0);
	}
	
	if (ifp->if_type != IFT_ETHER) {
		ifnet_unlock();
		return (ENOENT);
	}

	TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		sdl = (struct sockaddr_dl*)ifa->ifa_addr;
		if (sdl != NULL && (sdl->sdl_family == AF_LINK) &&
		    (sdl->sdl_type == IFT_ETHER)) {
			bzero(&ifr->ifr_hwaddr, sizeof lsa);
			ifr->ifr_hwaddr.sa_family = ARPHRD_ETHER;
			bcopy(LLADDR(sdl), ifr->ifr_hwaddr.sa_data, LINUX_IFHWADDRLEN);
			ifnet_unlock();
			return (0);
		}
	}

	ifnet_unlock();
	return (ENOENT);
}

static int
linux_ioctl_map_ifname(struct file *fp, u_long cmd, u_long ocmd, caddr_t data, struct ucred *cred)
{
	struct ifnet *ifp;
	int error;
	char *oifname = (char *)data;
	char lifname[LINUX_IFNAMSIZ];

	KASSERT(LINUX_IFNAMSIZ == IFNAMSIZ,
	    ("%s(): LINUX_IFNAMSIZ != IFNAMSIZ", __func__));
	
	if (fp->f_type != DTYPE_SOCKET) {
		/*
		 *  XXX: I doubt this is correct because
		 *       we don't map the ifname
		 */
		/* not a socket - probably a tap / vmnet device */
		if (ocmd == LINUX_SIOCGIFADDR || ocmd == LINUX_SIOCSIFADDR) {
			cmd = (ocmd == LINUX_SIOCGIFADDR) ? SIOCGIFADDR : SIOCSIFADDR;
			return (fo_ioctl(fp, cmd, data, cred, NULL));
		} else
			return (ENOIOCTL);
	}

	/* Save the original ifname */
	bcopy(oifname, lifname, LINUX_IFNAMSIZ);
#ifdef DEBUG
	kprintf("%s(): ioctl %d on %.*s\n", __func__,
		(int)(cmd & 0xffff), LINUX_IFNAMSIZ, lifname);
#endif
	ifnet_lock();
	/* Replace linux ifname with bsd ifname */
	ifp = ifname_linux_to_bsd(lifname, oifname);
	if (ifp == NULL) {
		error = EINVAL;
		ifnet_unlock():
		goto clean_ifname;
	}
	ifnet_unlock():

#ifdef DEBUG
	kprintf("%s(): %s translated to %s\n", __func__,
		lifname, oifname);
#endif

	error = fo_ioctl(fp, cmd, data, cred, NULL);

clean_ifname:
	bcopy(lifname, oifname, LINUX_IFNAMSIZ);
	return (error);
}


/*
 * generic linux -> BSD syscall direction mapper
 */
u_long
linux_gen_dirmap(u_long lstart, u_long lend, u_long bstart, u_long bend, u_long cmd, u_long ocmd)
{
	static u_int32_t dirbits[4] = { IOC_VOID, IOC_IN, IOC_OUT, IOC_INOUT };

	return ((cmd & ~IOC_DIRMASK) | dirbits[ocmd >> 30]);
}


static struct ioctl_map_range linux_ioctl_map_entries[] = {
	/* disk ioctl */
	MAPPED_IOCTL_IOR(LINUX_BLKGETSIZE, linux_ioctl_BLKGETSIZE32, uint32_t),
	/* termio ioctl */
	MAPPED_IOCTL_IOR(LINUX_TCGETS, linux_ioctl_TCGETS, struct linux_termios),
	MAPPED_IOCTL_IOW(LINUX_TCSETS, linux_ioctl_TCSETS, struct linux_termios),
	MAPPED_IOCTL_IOW(LINUX_TCSETSW, linux_ioctl_TCSETSW, struct linux_termios),
	MAPPED_IOCTL_IOW(LINUX_TCSETSF, linux_ioctl_TCSETSF, struct linux_termios),
	MAPPED_IOCTL_IOR(LINUX_TCGETA, linux_ioctl_TCGETA, struct linux_termio),
	MAPPED_IOCTL_IOW(LINUX_TCSETA, linux_ioctl_TCSETA, struct linux_termio),
	MAPPED_IOCTL_IOW(LINUX_TCSETAW, linux_ioctl_TCSETAW, struct linux_termio),
	MAPPED_IOCTL_IOW(LINUX_TCSETAF, linux_ioctl_TCSETAF, struct linux_termio),
	MAPPED_IOCTL_IO(LINUX_TCXONC, linux_ioctl_TCXONC),
	MAPPED_IOCTL_IO(LINUX_TCFLSH, linux_ioctl_TCFLSH),
	MAPPED_IOCTL_IO(LINUX_TIOCLINUX, linux_ioctl_TIOCLINUX),
	MAPPED_IOCTL_MAP(LINUX_TIOCEXCL, TIOCEXCL),
	MAPPED_IOCTL_MAP(LINUX_TIOCNXCL, TIOCNXCL),
	MAPPED_IOCTL_MAP(LINUX_TIOCGPGRP, TIOCGPGRP),
	MAPPED_IOCTL_MAP(LINUX_TIOCSPGRP, TIOCSPGRP),
	MAPPED_IOCTL_MAP(LINUX_TIOCGWINSZ, TIOCGWINSZ),
	MAPPED_IOCTL_MAP(LINUX_TIOCSWINSZ, TIOCSWINSZ),
	MAPPED_IOCTL_MAP(LINUX_TIOCMGET, TIOCMGET),
	MAPPED_IOCTL_MAP(LINUX_TIOCMBIS, TIOCMBIS),
	MAPPED_IOCTL_MAP(LINUX_TIOCMBIC, TIOCMBIC),
	MAPPED_IOCTL_MAP(LINUX_TIOCMSET, TIOCMSET),
	MAPPED_IOCTL_MAP(LINUX_FIONREAD, FIONREAD),
	MAPPED_IOCTL_MAP(LINUX_TIOCCONS, TIOCCONS),
	MAPPED_IOCTL_IOR(LINUX_TIOCGSERIAL, linux_ioctl_TIOCGSERIAL, struct linux_serial_struct),
	MAPPED_IOCTL_IOW(LINUX_TIOCSSERIAL, linux_ioctl_TIOCSSERIAL, struct linux_serial_struct),
	MAPPED_IOCTL_MAP(LINUX_FIONBIO, FIONBIO),
	MAPPED_IOCTL_MAP(LINUX_TIOCNOTTY, TIOCNOTTY),
	MAPPED_IOCTL_IO(LINUX_TIOCSETD, linux_ioctl_TIOCSETD),
	MAPPED_IOCTL_IOR(LINUX_TIOCGETD, linux_ioctl_TIOCGETD, int),
	MAPPED_IOCTL_MAP(LINUX_FIONCLEX, FIONCLEX),
	MAPPED_IOCTL_MAP(LINUX_FIOCLEX, FIOCLEX),
	MAPPED_IOCTL_MAP(LINUX_FIOASYNC, FIOASYNC),
	/* cdrom ioctl */
	MAPPED_IOCTL_MAP(LINUX_CDROMPAUSE, CDIOCPAUSE),
	MAPPED_IOCTL_MAP(LINUX_CDROMRESUME, CDIOCRESUME),
	MAPPED_IOCTL_MAP(LINUX_CDROMPLAYMSF, CDIOCPLAYMSF),
	MAPPED_IOCTL_MAP(LINUX_CDROMPLAYTRKIND, CDIOCPLAYTRACKS),
	MAPPED_IOCTL_IOR(LINUX_CDROMREADTOCHDR, linux_ioctl_CDROMREADTOCHDR, struct linux_cdrom_tochdr),
	MAPPED_IOCTL_IOWR(LINUX_CDROMREADTOCENTRY, linux_ioctl_CDROMREADTOCENTRY, struct linux_cdrom_tocentry),
	MAPPED_IOCTL_MAP(LINUX_CDROMSTOP, CDIOCSTOP),
	MAPPED_IOCTL_MAP(LINUX_CDROMSTART, CDIOCSTART),
	MAPPED_IOCTL_MAP(LINUX_CDROMEJECT, CDIOCEJECT),
	MAPPED_IOCTL_IOWR(LINUX_CDROMSUBCHNL, linux_ioctl_CDROMSUBCHNL, struct linux_cdrom_subchnl),
	MAPPED_IOCTL_MAP(LINUX_CDROMRESET, CDIOCRESET),
	/* sound ioctl */
	MAPPED_IOCTL_MAPRANGE(LINUX_SOUND_MIXER_WRITE_MIN, LINUX_SOUND_MIXER_WRITE_MAX,
			      LINUX_SOUND_MIXER_WRITE_MIN, LINUX_SOUND_MIXER_WRITE_MAX,
			      NULL, linux_gen_dirmap),
	MAPPED_IOCTL_IOR(LINUX_OSS_GETVERSION, linux_ioctl_OSS_GETVERSION, int),
	MAPPED_IOCTL_MAP(LINUX_SOUND_MIXER_READ_DEVMASK, SOUND_MIXER_READ_DEVMASK),
	MAPPED_IOCTL_MAPRANGE(LINUX_SNDCTL_DSP_MIN, LINUX_SNDCTL_DSP_MAX, LINUX_SNDCTL_DSP_MIN,
			      LINUX_SNDCTL_DSP_MAX, NULL, linux_gen_dirmap),
	MAPPED_IOCTL_MAPRANGE(LINUX_SNDCTL_SEQ_MIN, LINUX_SNDCTL_SEQ_MAX, LINUX_SNDCTL_SEQ_MIN,
			      LINUX_SNDCTL_SEQ_MAX, NULL, linux_gen_dirmap),
	/* console ioctl */
	MAPPED_IOCTL_MAP(LINUX_KIOCSOUND, KIOCSOUND),
	MAPPED_IOCTL_MAP(LINUX_KDMKTONE, KDMKTONE),
	MAPPED_IOCTL_MAP(LINUX_KDGETLED, KDGETLED),
	MAPPED_IOCTL_MAP(LINUX_KDSETLED, KDSETLED),
	MAPPED_IOCTL_MAP(LINUX_KDSETMODE, KDSETMODE),
	MAPPED_IOCTL_MAP(LINUX_KDGETMODE, KDGETMODE),
	MAPPED_IOCTL_MAP(LINUX_KDGKBMODE, KDGKBMODE),
	MAPPED_IOCTL_IOW(LINUX_KDSKBMODE, linux_ioctl_KDSKBMODE, int),
	MAPPED_IOCTL_MAP(LINUX_VT_OPENQRY, VT_OPENQRY),
	MAPPED_IOCTL_MAP(LINUX_VT_GETMODE, VT_GETMODE),
	MAPPED_IOCTL_IOW(LINUX_VT_SETMODE, linux_ioctl_VT_SETMODE, struct vt_mode),
	MAPPED_IOCTL_MAP(LINUX_VT_GETSTATE, VT_GETACTIVE),
	MAPPED_IOCTL_MAP(LINUX_VT_RELDISP, VT_RELDISP),
	MAPPED_IOCTL_MAP(LINUX_VT_ACTIVATE, VT_ACTIVATE),
	MAPPED_IOCTL_MAP(LINUX_VT_WAITACTIVE, VT_WAITACTIVE),
	/* socket ioctl */
	MAPPED_IOCTL_MAP(LINUX_FIOSETOWN, FIOSETOWN),
	MAPPED_IOCTL_MAP(LINUX_SIOCSPGRP, SIOCSPGRP),
	MAPPED_IOCTL_MAP(LINUX_FIOGETOWN, FIOGETOWN),
	MAPPED_IOCTL_MAP(LINUX_SIOCGPGRP, SIOCGPGRP),
	MAPPED_IOCTL_MAP(LINUX_SIOCATMARK, SIOCATMARK),
	MAPPED_IOCTL_IOWR(LINUX_SIOCGIFCONF, linux_ioctl_SIOCGIFCONF, struct ifconf),
	MAPPED_IOCTL_IOWR(LINUX_SIOCGIFFLAGS, linux_ioctl_SIOCGIFFLAGS, struct l_ifreq),
	MAPPED_IOCTL_MAPF(LINUX_SIOCGIFADDR, OSIOCGIFADDR, linux_ioctl_map_ifname),
	MAPPED_IOCTL_MAPF(LINUX_SIOCSIFADDR, SIOCSIFADDR, linux_ioctl_map_ifname),
	MAPPED_IOCTL_MAPF(LINUX_SIOCGIFDSTADDR, OSIOCGIFDSTADDR, linux_ioctl_map_ifname),
	MAPPED_IOCTL_MAPF(LINUX_SIOCGIFBRDADDR, OSIOCGIFBRDADDR, linux_ioctl_map_ifname),
	MAPPED_IOCTL_MAPF(LINUX_SIOCGIFNETMASK, OSIOCGIFNETMASK, linux_ioctl_map_ifname),
	/*MAPPED_IOCTL_IOx(LINUX_SIOCSIFNETMASK, x, x),*/
	MAPPED_IOCTL_MAPF(LINUX_SIOCGIFMTU, SIOCGIFMTU, linux_ioctl_map_ifname),
	MAPPED_IOCTL_MAPF(LINUX_SIOCSIFMTU, SIOCSIFMTU, linux_ioctl_map_ifname),
	MAPPED_IOCTL_IOWR(LINUX_SIOCGIFHWADDR, linux_ioctl_SIOGIFHWADDR, struct l_ifreq),
	MAPPED_IOCTL_IOR(LINUX_SIOCGIFINDEX, linux_ioctl_SIOCGIFINDEX, struct l_ifreq),
	MAPPED_IOCTL_IOR(LINUX_SIOCGIFMETRIC, linux_ioctl_SIOCGIFMETRIC, struct l_ifreq),
	MAPPED_IOCTL_MAP(LINUX_SIOCADDMULTI, SIOCADDMULTI),
	MAPPED_IOCTL_MAP(LINUX_SIOCDELMULTI, SIOCDELMULTI),
	/*
	 * XXX This is slightly bogus, but these ioctls are currently
	 * XXX only used by the aironet (if_an) network driver.
	 */
	MAPPED_IOCTL_MAPF(LINUX_SIOCDEVPRIVATE, SIOCGPRIVATE_0, linux_ioctl_map_ifname),
	MAPPED_IOCTL_MAPF(LINUX_SIOCDEVPRIVATE+1, SIOCGPRIVATE_1, linux_ioctl_map_ifname),
	MAPPED_IOCTL_MAPF(0, 0, NULL)
	};

struct ioctl_map linux_ioctl_map = {
	0xffff,		/* mask */
	"linux",	/* subsys */
	LIST_HEAD_INITIALIZER(mapping)
	};

static struct ioctl_map_handler linux_ioctl_base_handler = {
	&linux_ioctl_map,
	"base",
	linux_ioctl_map_entries
	};

/*
 * main ioctl syscall function
 *
 * MPALMOSTSAFE
 */
int
sys_linux_ioctl(struct linux_ioctl_args *args)
{
	int error;

#ifdef DEBUG
	if (ldebug(ioctl))
		kprintf(ARGS(ioctl, "%d, %04x, *"), args->fd, args->cmd);
#endif

	get_mplock();
	error = mapped_ioctl(args->fd, args->cmd, (caddr_t)args->arg,
			     &linux_ioctl_map, &args->sysmsg);
	rel_mplock();
	return (error);
}

SYSINIT  (linux_ioctl_register, SI_BOOT2_KLD, SI_ORDER_MIDDLE,
	  mapped_ioctl_register_handler, &linux_ioctl_base_handler);
SYSUNINIT(linux_ioctl_register, SI_BOOT2_KLD, SI_ORDER_MIDDLE,
	  mapped_ioctl_unregister_handler, &linux_ioctl_base_handler);
