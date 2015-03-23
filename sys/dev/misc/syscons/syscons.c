/*-
 * Copyright (c) 1992-1998 Søren Schmidt
 * All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sascha Wildner <saw@online.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD: /usr/local/www/cvsroot/FreeBSD/src/sys/dev/syscons/syscons.c,v 1.336.2.17 2004/03/25 08:41:09 ru Exp $
 */

#include "use_splash.h"
#include "opt_syscons.h"
#include "opt_ddb.h"
#ifdef __i386__
#include "use_apm.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/tty.h>
#include <sys/kernel.h>
#include <sys/cons.h>
#include <sys/random.h>

#include <sys/thread2.h>
#include <sys/mutex2.h>

#include <machine/clock.h>
#include <machine/console.h>
#include <machine/psl.h>
#include <machine/pc/display.h>
#ifdef __i386__
#include <machine/apm_bios.h>
#endif
#include <machine/frame.h>

#include <dev/misc/kbd/kbdreg.h>
#include <dev/video/fb/fbreg.h>
#include <dev/video/fb/splashreg.h>
#include "syscons.h"

#define COLD 0
#define WARM 1

#define DEFAULT_BLANKTIME	(5*60)		/* 5 minutes */
#define MAX_BLANKTIME		(7*24*60*60)	/* 7 days!? */

#define KEYCODE_BS		0x0e		/* "<-- Backspace" key, XXX */
#define WANT_UNLOCK(m) do {	  \
	if (m)			  \
		syscons_unlock(); \
} while (0)

#define WANT_LOCK(m) do { 	  \
	if (m)			  \
		syscons_lock();	  \
} while(0)


MALLOC_DEFINE(M_SYSCONS, "syscons", "Syscons");

typedef struct default_attr {
	int		std_color;		/* normal hardware color */
	int		rev_color;		/* reverse hardware color */
} default_attr;

static default_attr user_default = {
    SC_NORM_ATTR,
    SC_NORM_REV_ATTR,
};

static default_attr kernel_default = {
    SC_KERNEL_CONS_ATTR,
    SC_KERNEL_CONS_REV_ATTR,
};

static	int		sc_console_unit = -1;
static  scr_stat    	*sc_console;
static	struct tty	*sc_console_tty;
static	void		*kernel_console_ts;

static  char        	init_done = COLD;
static  char		shutdown_in_progress = FALSE;
static	char		sc_malloc = FALSE;

static	int		saver_mode = CONS_NO_SAVER; /* LKM/user saver */
static	int		run_scrn_saver = FALSE;	/* should run the saver? */
static	long        	scrn_blank_time = 0;    /* screen saver timeout value */
#if NSPLASH > 0
static	int     	scrn_blanked;		/* # of blanked screen */
static	int		sticky_splash = FALSE;

static	void		none_saver(sc_softc_t *sc, int blank) { }
static	void		(*current_saver)(sc_softc_t *, int) = none_saver;
#endif

#if !defined(SC_NO_FONT_LOADING) && defined(SC_DFLT_FONT)
#include "font.h"
#endif

static	bios_values_t	bios_value;

static	int		enable_panic_key;
SYSCTL_INT(_machdep, OID_AUTO, enable_panic_key, CTLFLAG_RW, &enable_panic_key,
	   0, "Enable the panic key (CTRL-ALT-SHIFT-ESC)");

#define SC_CONSOLECTL	255

#define VIRTUAL_TTY(sc, x) ((SC_DEV((sc),(x)) != NULL) ?	\
	(SC_DEV((sc),(x))->si_tty) : NULL)
#define ISTTYOPEN(tp)	((tp) && ((tp)->t_state & TS_ISOPEN))

static	int	debugger;
static	cdev_t	cctl_dev;
#if 0
static	timeout_t blink_screen_callout;
#endif
static  void	sc_blink_screen(scr_stat *scp);
static	struct mtx	syscons_mtx = MTX_INITIALIZER("syscons");

/* prototypes */
static int scvidprobe(int unit, int flags, int cons);
static int sckbdprobe(int unit, int flags, int cons);
static void scmeminit(void *arg);
static int scdevtounit(cdev_t dev);
static kbd_callback_func_t sckbdevent;
static int scparam(struct tty *tp, struct termios *t);
static void scstart(struct tty *tp);
static void scinit(int unit, int flags);
static void scterm(int unit, int flags);
static void scshutdown(void *arg, int howto);
static void sc_puts(scr_stat *scp, u_char *buf, int len);
static u_int scgetc(sc_softc_t *sc, u_int flags);
#define SCGETC_CN	1
#define SCGETC_NONBLOCK	2
static int sccngetch(int flags);
static void sccnupdate(scr_stat *scp);
static scr_stat *alloc_scp(sc_softc_t *sc, int vty);
static void init_scp(sc_softc_t *sc, int vty, scr_stat *scp);
static timeout_t scrn_timer;
static int and_region(int *s1, int *e1, int s2, int e2);
static void scrn_update(scr_stat *scp, int show_cursor);

#if NSPLASH > 0
static int scsplash_callback(int event, void *arg);
static void scsplash_saver(sc_softc_t *sc, int show);
static int add_scrn_saver(void (*this_saver)(sc_softc_t *, int));
static int remove_scrn_saver(void (*this_saver)(sc_softc_t *, int));
static int set_scrn_saver_mode(scr_stat *scp, int mode, u_char *pal, int border);
static int restore_scrn_saver_mode(scr_stat *scp, int changemode);
static void stop_scrn_saver(sc_softc_t *sc, void (*saver)(sc_softc_t *, int));
static int wait_scrn_saver_stop(sc_softc_t *sc);
#define scsplash_stick(stick)		(sticky_splash = (stick))
#else /* !NSPLASH */
#define scsplash_stick(stick)
#endif /* NSPLASH */

static void do_switch_scr(sc_softc_t *sc);
static int vt_proc_alive(scr_stat *scp);
static int signal_vt_rel(scr_stat *scp);
static int signal_vt_acq(scr_stat *scp);
static int finish_vt_rel(scr_stat *scp, int release);
static int finish_vt_acq(scr_stat *scp);
static void exchange_scr(sc_softc_t *sc);
static void update_cursor_image(scr_stat *scp);
static int save_kbd_state(scr_stat *scp, int unlock);
static int update_kbd_state(scr_stat *scp, int state, int mask, int unlock);
static int update_kbd_leds(scr_stat *scp, int which);
static int sc_allocate_keyboard(sc_softc_t *sc, int unit);

/*
 * Console locking support functions.
 *
 * We use mutex spinlocks here in order to allow reentrancy which should
 * avoid issues during panics.
 */
static void
syscons_lock(void)
{
	mtx_spinlock(&syscons_mtx);
}

/*
 * Returns 0 on success, EAGAIN on failure.
 */
static int
syscons_lock_nonblock(void)
{
	return(mtx_spinlock_try(&syscons_mtx));
}

static void
syscons_unlock(void)
{
	mtx_spinunlock(&syscons_mtx);
}

/*
 * Console driver
 */
static cn_probe_t	sccnprobe;
static cn_init_t	sccninit;
static cn_init_t	sccninit_fini;
static cn_getc_t	sccngetc;
static cn_checkc_t	sccncheckc;
static cn_putc_t	sccnputc;
static cn_dbctl_t	sccndbctl;
static cn_term_t	sccnterm;

CONS_DRIVER(sc, sccnprobe, sccninit, sccninit_fini, sccnterm,
	    sccngetc, sccncheckc, sccnputc, sccndbctl);

static	d_open_t	scopen;
static	d_close_t	scclose;
static	d_read_t	scread;
static	d_ioctl_t	scioctl;
static	d_mmap_t	scmmap;

static struct dev_ops sc_ops = {
	{ "sc", 0, D_TTY },
	.d_open =	scopen,
	.d_close =	scclose,
	.d_read =	scread,
	.d_write =	ttywrite,
	.d_ioctl =	scioctl,
	.d_mmap =	scmmap,
	.d_kqfilter =	ttykqfilter,
	.d_revoke =	ttyrevoke
};

int
sc_probe_unit(int unit, int flags)
{
    if (!scvidprobe(unit, flags, FALSE)) {
	if (bootverbose)
	    kprintf("sc%d: no video adapter found.\n", unit);
	return ENXIO;
    }

    /* syscons will be attached even when there is no keyboard */
    sckbdprobe(unit, flags, FALSE);

    return 0;
}

/* probe video adapters, return TRUE if found */ 
static int
scvidprobe(int unit, int flags, int cons)
{
    /*
     * Access the video adapter driver through the back door!
     * Video adapter drivers need to be configured before syscons.
     * However, when syscons is being probed as the low-level console,
     * they have not been initialized yet.  We force them to initialize
     * themselves here. XXX
     */
    vid_configure(cons ? VIO_PROBE_ONLY : 0);

    return (vid_find_adapter("*", unit) >= 0);
}

/* probe the keyboard, return TRUE if found */
static int
sckbdprobe(int unit, int flags, int cons)
{
    /* access the keyboard driver through the backdoor! */
    kbd_configure(cons ? KB_CONF_PROBE_ONLY : 0);

    return (kbd_find_keyboard("*", unit) >= 0);
}

static char *
adapter_name(video_adapter_t *adp)
{
    static struct {
	int type;
	char *name[2];
    } names[] = {
	{ KD_MONO,	{ "MDA",	"MDA" } },
	{ KD_HERCULES,	{ "Hercules",	"Hercules" } },
	{ KD_CGA,	{ "CGA",	"CGA" } },
	{ KD_EGA,	{ "EGA",	"EGA (mono)" } },
	{ KD_VGA,	{ "VGA",	"VGA (mono)" } },
	{ KD_TGA,	{ "TGA",	"TGA" } },
	{ -1,		{ "Unknown",	"Unknown" } },
    };
    int i;

    for (i = 0; names[i].type != -1; ++i)
	if (names[i].type == adp->va_type)
	    break;
    return names[i].name[(adp->va_flags & V_ADP_COLOR) ? 0 : 1];
}

int
sc_attach_unit(int unit, int flags)
{
    sc_softc_t *sc;
    scr_stat *scp;
#ifdef SC_PIXEL_MODE
    video_info_t info;
#endif
    int vc;
    cdev_t dev;
    flags &= ~SC_KERNEL_CONSOLE;

    if (sc_console_unit == unit) {
	/*
	 * If this unit is being used as the system console, we need to
	 * adjust some variables and buffers before and after scinit().
	 */
	/* assert(sc_console != NULL) */
	flags |= SC_KERNEL_CONSOLE;
	scmeminit(NULL);

	scinit(unit, flags);

	if (sc_console->tsw->te_size > 0) {
	    /* assert(sc_console->ts != NULL); */
	    kernel_console_ts = sc_console->ts;
	    sc_console->ts = kmalloc(sc_console->tsw->te_size,
				    M_SYSCONS, M_WAITOK);
	    bcopy(kernel_console_ts, sc_console->ts, sc_console->tsw->te_size);
    	    (*sc_console->tsw->te_default_attr)(sc_console,
						user_default.std_color,
						user_default.rev_color);
	}
    } else {
	scinit(unit, flags);
    }

    sc = sc_get_softc(unit, flags & SC_KERNEL_CONSOLE);

    /*
     * If this is the console we couldn't setup sc->dev before because
     * malloc wasn't working.  Set it up now.
     */
    if (flags & SC_KERNEL_CONSOLE) {
	KKASSERT(sc->dev == NULL);
	sc->dev = kmalloc(sizeof(cdev_t)*sc->vtys, M_SYSCONS, M_WAITOK|M_ZERO);
	sc->dev[0] = make_dev(&sc_ops, sc_console_unit*MAXCONS, UID_ROOT, 
			      GID_WHEEL, 0600, 
			      "ttyv%r", sc_console_unit*MAXCONS);
	sc->dev[0]->si_tty = ttymalloc(sc->dev[0]->si_tty);
	sc->dev[0]->si_drv1 = sc_console;
    }

    /*
     * Finish up the standard attach
     */
    sc->config = flags;
    callout_init_mp(&sc->scrn_timer_ch);
    scp = SC_STAT(sc->dev[0]);
    if (sc_console == NULL)	/* sc_console_unit < 0 */
	sc_console = scp;

#ifdef SC_PIXEL_MODE
    if ((sc->config & SC_VESA800X600)
	&& ((*vidsw[sc->adapter]->get_info)(sc->adp, M_VESA_800x600, &info) == 0)) {
#if NSPLASH > 0
	if (sc->flags & SC_SPLASH_SCRN)
	    splash_term(sc->adp);
#endif
	sc_set_graphics_mode(scp, NULL, M_VESA_800x600);
	sc_set_pixel_mode(scp, NULL, 0, 0, 16);
	sc->initial_mode = M_VESA_800x600;
#if NSPLASH > 0
	/* put up the splash again! */
	if (sc->flags & SC_SPLASH_SCRN)
    	    splash_init(sc->adp, scsplash_callback, sc);
#endif
    }
#endif /* SC_PIXEL_MODE */

    /* initialize cursor */
    if (!ISGRAPHSC(scp))
    	update_cursor_image(scp);

    /* get screen update going */
    scrn_timer(sc);

    /* set up the keyboard */
    kbd_ioctl(sc->kbd, KDSKBMODE, (caddr_t)&scp->kbd_mode);
    update_kbd_state(scp, scp->status, LOCK_MASK, FALSE);

    kprintf("sc%d: %s <%d virtual consoles, flags=0x%x>\n",
	   unit, adapter_name(sc->adp), sc->vtys, sc->config);
    if (bootverbose) {
	kprintf("sc%d:", unit);
    	if (sc->adapter >= 0)
	    kprintf(" fb%d", sc->adapter);
	if (sc->keyboard >= 0)
	    kprintf(", kbd%d", sc->keyboard);
	if (scp->tsw)
	    kprintf(", terminal emulator: %s (%s)",
		   scp->tsw->te_name, scp->tsw->te_desc);
	kprintf("\n");
    }

    /* register a shutdown callback for the kernel console */
    if (sc_console_unit == unit)
	EVENTHANDLER_REGISTER(shutdown_pre_sync, scshutdown, 
			      (void *)(uintptr_t)unit, SHUTDOWN_PRI_DEFAULT);

    /* 
     * create devices.
     *
     * The first vty already has struct tty and scr_stat initialized
     * in scinit().  The other vtys will have these structs when
     * first opened.
     */
    for (vc = 1; vc < sc->vtys; vc++) {
	dev = make_dev(&sc_ops, vc + unit * MAXCONS,
			UID_ROOT, GID_WHEEL,
			0600, "ttyv%r", vc + unit * MAXCONS);
	sc->dev[vc] = dev;
    }
    cctl_dev = make_dev(&sc_ops, SC_CONSOLECTL,
			UID_ROOT, GID_WHEEL, 0600, "consolectl");
    cctl_dev->si_tty = sc_console_tty = ttymalloc(sc_console_tty);
    cctl_dev->si_drv1 = sc_console;
    return 0;
}

static void
scmeminit(void *arg)
{
    if (sc_malloc)
	return;
    sc_malloc = TRUE;

    /*
     * As soon as malloc() becomes functional, we had better allocate
     * various buffers for the kernel console.
     */

    if (sc_console_unit < 0)	/* sc_console == NULL */
	return;

    /* copy the temporary buffer to the final buffer */
    sc_alloc_scr_buffer(sc_console, TRUE, FALSE);

#ifndef SC_NO_CUTPASTE
    sc_alloc_cut_buffer(sc_console, TRUE);
#endif

#ifndef SC_NO_HISTORY
    /* initialize history buffer & pointers */
    sc_alloc_history_buffer(sc_console, 0, 0, TRUE);
#endif
}

SYSINIT(sc_mem, SI_BOOT1_POST, SI_ORDER_ANY, scmeminit, NULL);

static int
scdevtounit(cdev_t dev)
{
    int vty = SC_VTY(dev);

    if (vty == SC_CONSOLECTL)
	return ((sc_console != NULL) ? sc_console->sc->unit : -1);
    else if ((vty < 0) || (vty >= MAXCONS*sc_max_unit()))
	return -1;
    else
	return vty/MAXCONS;
}

int
scopen(struct dev_open_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    int unit;
    sc_softc_t *sc;
    struct tty *tp;
    scr_stat *scp;
    keyarg_t key;
    int error;

    lwkt_gettoken(&tty_token);
    unit = scdevtounit(dev);
    DPRINTF(5, ("scopen: dev:%d,%d, unit:%d, vty:%d\n",
		major(dev), minor(dev), unit, SC_VTY(dev)));

    sc = sc_get_softc(unit, (sc_console_unit == unit) ? SC_KERNEL_CONSOLE : 0);
    if (sc == NULL) {
	lwkt_reltoken(&tty_token);
	return ENXIO;
    }

    tp = dev->si_tty = ttymalloc(dev->si_tty);
    tp->t_oproc = scstart;
    tp->t_param = scparam;
    tp->t_stop = nottystop;

    tp->t_dev = dev;

    if (!ISTTYOPEN(tp)) {
	ttychars(tp);
        /* Use the current setting of the <-- key as default VERASE. */  
        /* If the Delete key is preferable, an stty is necessary     */
	if (sc->kbd != NULL) {
	    key.keynum = KEYCODE_BS;
	    kbd_ioctl(sc->kbd, GIO_KEYMAPENT, (caddr_t)&key);
            tp->t_cc[VERASE] = key.key.map[0];
	}
	tp->t_iflag = TTYDEF_IFLAG;
	tp->t_oflag = TTYDEF_OFLAG;
	tp->t_cflag = TTYDEF_CFLAG;
	tp->t_lflag = TTYDEF_LFLAG;
	tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
	scparam(tp, &tp->t_termios);
	(*linesw[tp->t_line].l_modem)(tp, 1);
    }
    else
	if (tp->t_state & TS_XCLUDE && priv_check_cred(ap->a_cred, PRIV_ROOT, 0)) {
	    lwkt_reltoken(&tty_token);
	    return(EBUSY);
	}

    error = (*linesw[tp->t_line].l_open)(dev, tp);

    scp = SC_STAT(dev);
    if (scp == NULL) {
	scp = dev->si_drv1 = alloc_scp(sc, SC_VTY(dev));
	syscons_lock();
	if (ISGRAPHSC(scp))
	    sc_set_pixel_mode(scp, NULL, COL, ROW, 16);
	syscons_unlock();
    }
    if (!tp->t_winsize.ws_col && !tp->t_winsize.ws_row) {
	tp->t_winsize.ws_col = scp->xsize;
	tp->t_winsize.ws_row = scp->ysize;
    }

    lwkt_reltoken(&tty_token);
    return error;
}

int
scclose(struct dev_close_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    struct tty *tp = dev->si_tty;
    scr_stat *scp;

    lwkt_gettoken(&tty_token);
    if (SC_VTY(dev) != SC_CONSOLECTL) {
	scp = SC_STAT(tp->t_dev);
	/* were we in the middle of the VT switching process? */
	DPRINTF(5, ("sc%d: scclose(), ", scp->sc->unit));
	if ((scp == scp->sc->cur_scp) && (scp->sc->unit == sc_console_unit))
	    cons_unavail = FALSE;
	if (finish_vt_rel(scp, TRUE) == 0)	/* force release */
	    DPRINTF(5, ("reset WAIT_REL, "));
	if (finish_vt_acq(scp) == 0)		/* force acknowledge */
	    DPRINTF(5, ("reset WAIT_ACQ, "));
	syscons_lock();
#if 0 /* notyet */
	if (scp == &main_console) {
	    scp->pid = 0;
	    scp->proc = NULL;
	    scp->smode.mode = VT_AUTO;
	}
	else {
	    sc_vtb_destroy(&scp->vtb);
	    sc_vtb_destroy(&scp->scr);
	    sc_free_history_buffer(scp, scp->ysize);
	    SC_STAT(dev) = NULL;
	    kfree(scp, M_SYSCONS);
	}
#else
	scp->pid = 0;
	scp->proc = NULL;
	scp->smode.mode = VT_AUTO;
#endif
	scp->kbd_mode = K_XLATE;
	syscons_unlock();
	if (scp == scp->sc->cur_scp)
	    kbd_ioctl(scp->sc->kbd, KDSKBMODE, (caddr_t)&scp->kbd_mode);
	DPRINTF(5, ("done.\n"));
    }
    (*linesw[tp->t_line].l_close)(tp, ap->a_fflag);
    ttyclose(tp);
    lwkt_reltoken(&tty_token);

    return(0);
}

int
scread(struct dev_read_args *ap)
{
    int ret;

    lwkt_gettoken(&tty_token);
    sc_touch_scrn_saver();
    ret = ttyread(ap);
    lwkt_reltoken(&tty_token);
    return ret;
}

static int
sckbdevent(keyboard_t *thiskbd, int event, void *arg)
{
    sc_softc_t *sc;
    struct tty *cur_tty;
    int c; 
    size_t len;
    u_char *cp;

    lwkt_gettoken(&tty_token);
    /*
     * WARNING: In early boot sc->dev may not be setup yet.
     */
    sc = (sc_softc_t *)arg;
    /* assert(thiskbd == sc->kbd) */

    switch (event) {
    case KBDIO_KEYINPUT:
	break;
    case KBDIO_UNLOADING:
	syscons_lock();
	sc->kbd = NULL;
	sc->keyboard = -1;
	syscons_unlock();
	kbd_release(thiskbd, (void *)&sc->keyboard);
	lwkt_reltoken(&tty_token);
	return 0;
    default:
        lwkt_reltoken(&tty_token);
	return EINVAL;
    }

    /* 
     * Loop while there is still input to get from the keyboard.
     * I don't think this is nessesary, and it doesn't fix
     * the Xaccel-2.1 keyboard hang, but it can't hurt.		XXX
     */
    while ((c = scgetc(sc, SCGETC_NONBLOCK)) != NOKEY) {
	cur_tty = VIRTUAL_TTY(sc, sc->cur_scp->index);
	if (!ISTTYOPEN(cur_tty)) {
	    cur_tty = sc_console_tty;
	    if (!ISTTYOPEN(cur_tty))
		continue;
	}

	syscons_lock();
	if ((*sc->cur_scp->tsw->te_input)(sc->cur_scp, c, cur_tty)) {
	    syscons_unlock();
	    continue;
	}
	syscons_unlock();

	switch (KEYFLAGS(c)) {
	case 0x0000: /* normal key */
	    (*linesw[cur_tty->t_line].l_rint)(KEYCHAR(c), cur_tty);
	    break;
	case FKEY:  /* function key, return string */
	    cp = kbd_get_fkeystr(thiskbd, KEYCHAR(c), &len);
	    if (cp != NULL) {
	    	while (len-- >  0)
		    (*linesw[cur_tty->t_line].l_rint)(*cp++, cur_tty);
	    }
	    break;
	case MKEY:  /* meta is active, prepend ESC */
	    (*linesw[cur_tty->t_line].l_rint)(0x1b, cur_tty);
	    (*linesw[cur_tty->t_line].l_rint)(KEYCHAR(c), cur_tty);
	    break;
	case BKEY:  /* backtab fixed sequence (esc [ Z) */
	    (*linesw[cur_tty->t_line].l_rint)(0x1b, cur_tty);
	    (*linesw[cur_tty->t_line].l_rint)('[', cur_tty);
	    (*linesw[cur_tty->t_line].l_rint)('Z', cur_tty);
	    break;
	}
    }

    syscons_lock();
    sc->cur_scp->status |= MOUSE_HIDDEN;
    syscons_unlock();

    lwkt_reltoken(&tty_token);
    return 0;
}

static int
scparam(struct tty *tp, struct termios *t)
{
    lwkt_gettoken(&tty_token);
    tp->t_ispeed = t->c_ispeed;
    tp->t_ospeed = t->c_ospeed;
    tp->t_cflag = t->c_cflag;
    lwkt_reltoken(&tty_token);
    return 0;
}

int
scioctl(struct dev_ioctl_args *ap)
{
    cdev_t dev = ap->a_head.a_dev;
    u_long cmd = ap->a_cmd;
    caddr_t data = ap->a_data;
    int flag = ap->a_fflag;
    int error;
    int i;
    struct tty *tp;
    sc_softc_t *sc;
    scr_stat *scp;

    lwkt_gettoken(&tty_token);
    tp = dev->si_tty;

    error = sc_vid_ioctl(tp, cmd, data, flag);
    if (error != ENOIOCTL) {
        lwkt_reltoken(&tty_token);
	return error;
    }

#ifndef SC_NO_HISTORY
    error = sc_hist_ioctl(tp, cmd, data, flag);
    if (error != ENOIOCTL) {
        lwkt_reltoken(&tty_token);
	return error;
    }
#endif

#ifndef SC_NO_SYSMOUSE
    error = sc_mouse_ioctl(tp, cmd, data, flag);
    if (error != ENOIOCTL) {
        lwkt_reltoken(&tty_token);
	return error;
    }
#endif

    scp = SC_STAT(tp->t_dev);
    /* assert(scp != NULL) */
    /* scp is sc_console, if SC_VTY(dev) == SC_CONSOLECTL. */
    sc = scp->sc;

    if (scp->tsw) {
	syscons_lock();
	error = (*scp->tsw->te_ioctl)(scp, tp, cmd, data, flag);
	syscons_unlock();
	if (error != ENOIOCTL) {
	    lwkt_reltoken(&tty_token);
	    return error;
	}
    }

    switch (cmd) {  		/* process console hardware related ioctl's */

    case GIO_ATTR:      	/* get current attributes */
	/* this ioctl is not processed here, but in the terminal emulator */
	lwkt_reltoken(&tty_token);
	return ENOTTY;

    case GIO_COLOR:     	/* is this a color console ? */
	*(int *)data = (sc->adp->va_flags & V_ADP_COLOR) ? 1 : 0;
	lwkt_reltoken(&tty_token);
	return 0;

    case CONS_BLANKTIME:    	/* set screen saver timeout (0 = no saver) */
	if (*(int *)data < 0 || *(int *)data > MAX_BLANKTIME) {
	    lwkt_reltoken(&tty_token);
            return EINVAL;
	}
	syscons_lock();
	scrn_blank_time = *(int *)data;
	run_scrn_saver = (scrn_blank_time != 0);
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return 0;

    case CONS_CURSORTYPE:   	/* set cursor type blink/noblink */
	syscons_lock();
	if (!ISGRAPHSC(sc->cur_scp))
	    sc_remove_cursor_image(sc->cur_scp);
	if ((*(int*)data) & 0x01)
	    sc->flags |= SC_BLINK_CURSOR;
	else
	    sc->flags &= ~SC_BLINK_CURSOR;
	if ((*(int*)data) & 0x02) {
	    sc->flags |= SC_CHAR_CURSOR;
	} else
	    sc->flags &= ~SC_CHAR_CURSOR;
	/* 
	 * The cursor shape is global property; all virtual consoles
	 * are affected. Update the cursor in the current console...
	 */
	if (!ISGRAPHSC(sc->cur_scp)) {
	    sc_set_cursor_image(sc->cur_scp);
	    sc_draw_cursor_image(sc->cur_scp);
	}
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return 0;

    case CONS_BELLTYPE: 	/* set bell type sound/visual */
	syscons_lock();

	if ((*(int *)data) & 0x01)
	    sc->flags |= SC_VISUAL_BELL;
	else
	    sc->flags &= ~SC_VISUAL_BELL;

	if ((*(int *)data) & 0x02)
	    sc->flags |= SC_QUIET_BELL;
	else
	    sc->flags &= ~SC_QUIET_BELL;

	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return 0;

    case CONS_GETINFO:  	/* get current (virtual) console info */
    {
	vid_info_t *ptr = (vid_info_t*)data;
	if (ptr->size == sizeof(struct vid_info)) {
	    ptr->m_num = sc->cur_scp->index;
	    ptr->font_size = scp->font_size;
	    ptr->mv_col = scp->xpos;
	    ptr->mv_row = scp->ypos;
	    ptr->mv_csz = scp->xsize;
	    ptr->mv_rsz = scp->ysize;
	    /*
	     * The following fields are filled by the terminal emulator. XXX
	     *
	     * ptr->mv_norm.fore
	     * ptr->mv_norm.back
	     * ptr->mv_rev.fore
	     * ptr->mv_rev.back
	     */
	    ptr->mv_grfc.fore = 0;      /* not supported */
	    ptr->mv_grfc.back = 0;      /* not supported */
	    ptr->mv_ovscan = scp->border;
	    if (scp == sc->cur_scp)
	        save_kbd_state(scp, FALSE);
	    ptr->mk_keylock = scp->status & LOCK_MASK;
	    lwkt_reltoken(&tty_token);
	    return 0;
	}
	lwkt_reltoken(&tty_token);
	return EINVAL;
    }

    case CONS_GETVERS:  	/* get version number */
	*(int*)data = 0x200;    /* version 2.0 */
	lwkt_reltoken(&tty_token);
	return 0;

    case CONS_IDLE:		/* see if the screen has been idle */
	/*
	 * When the screen is in the GRAPHICS_MODE or UNKNOWN_MODE,
	 * the user process may have been writing something on the
	 * screen and syscons is not aware of it. Declare the screen
	 * is NOT idle if it is in one of these modes. But there is
	 * an exception to it; if a screen saver is running in the 
	 * graphics mode in the current screen, we should say that the
	 * screen has been idle.
	 */
	*(int *)data = (sc->flags & SC_SCRN_IDLE)
		       && (!ISGRAPHSC(sc->cur_scp)
			   || (sc->cur_scp->status & SAVER_RUNNING));
	lwkt_reltoken(&tty_token);
	return 0;

    case CONS_SAVERMODE:	/* set saver mode */
	switch(*(int *)data) {
	case CONS_NO_SAVER:
	case CONS_USR_SAVER:
	    syscons_lock();
	    /* if a LKM screen saver is running, stop it first. */
	    scsplash_stick(FALSE);
	    saver_mode = *(int *)data;
#if NSPLASH > 0
	    if ((error = wait_scrn_saver_stop(NULL))) {
		syscons_unlock();
		lwkt_reltoken(&tty_token);
		return error;
	    }
#endif /* NSPLASH */
	    run_scrn_saver = TRUE;
	    if (saver_mode == CONS_USR_SAVER)
		scp->status |= SAVER_RUNNING;
	    else
		scp->status &= ~SAVER_RUNNING;
	    scsplash_stick(TRUE);
	    syscons_unlock();
	    break;
	case CONS_LKM_SAVER:
	    syscons_lock();
	    if ((saver_mode == CONS_USR_SAVER) && (scp->status & SAVER_RUNNING))
		scp->status &= ~SAVER_RUNNING;
	    saver_mode = *(int *)data;
	    syscons_unlock();
	    break;
	default:
	    lwkt_reltoken(&tty_token);
	    return EINVAL;
	}
	lwkt_reltoken(&tty_token);
	return 0;

    case CONS_SAVERSTART:	/* immediately start/stop the screen saver */
	/*
	 * Note that this ioctl does not guarantee the screen saver 
	 * actually starts or stops. It merely attempts to do so...
	 */
	syscons_lock();
	run_scrn_saver = (*(int *)data != 0);
	if (run_scrn_saver)
	    sc->scrn_time_stamp -= scrn_blank_time;
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return 0;

    case CONS_SCRSHOT:		/* get a screen shot */
    {
	scrshot_t *ptr = (scrshot_t*)data;
	syscons_lock();
	if (ISGRAPHSC(scp)) {
	    syscons_unlock();
	    lwkt_reltoken(&tty_token);
	    return EOPNOTSUPP;
	}
	if (scp->xsize != ptr->xsize || scp->ysize != ptr->ysize) {
	    syscons_unlock();
	    lwkt_reltoken(&tty_token);
	    return EINVAL;
	}
	syscons_unlock();
	copyout ((void*)scp->vtb.vtb_buffer, ptr->buf,
		 ptr->xsize * ptr->ysize * sizeof(uint16_t));
	lwkt_reltoken(&tty_token);
	return 0;
    }

    case VT_SETMODE:    	/* set screen switcher mode */
    {
	struct vt_mode *mode;

	mode = (struct vt_mode *)data;
	DPRINTF(5, ("sc%d: VT_SETMODE ", sc->unit));
	if (scp->smode.mode == VT_PROCESS) {
	    if (scp->proc == pfindn(scp->pid) && scp->proc != curproc) {
		DPRINTF(5, ("error EPERM\n"));
		lwkt_reltoken(&tty_token);
		return EPERM;
	    }
	}
	syscons_lock();
	if (mode->mode == VT_AUTO) {
	    scp->smode.mode = VT_AUTO;
	    scp->proc = NULL;
	    scp->pid = 0;
	    DPRINTF(5, ("VT_AUTO, "));
	    if ((scp == sc->cur_scp) && (sc->unit == sc_console_unit))
		cons_unavail = FALSE;
	    if (finish_vt_rel(scp, TRUE) == 0)
		DPRINTF(5, ("reset WAIT_REL, "));
	    if (finish_vt_acq(scp) == 0)
		DPRINTF(5, ("reset WAIT_ACQ, "));
	} else {
	    if (!ISSIGVALID(mode->relsig) || !ISSIGVALID(mode->acqsig)
		|| !ISSIGVALID(mode->frsig)) {
		syscons_unlock();
		DPRINTF(5, ("error EINVAL\n"));
		lwkt_reltoken(&tty_token);
		return EINVAL;
	    }
	    DPRINTF(5, ("VT_PROCESS %d, ", curproc->p_pid));
	    bcopy(data, &scp->smode, sizeof(struct vt_mode));
	    scp->proc = curproc;
	    scp->pid = scp->proc->p_pid;
	    if ((scp == sc->cur_scp) && (sc->unit == sc_console_unit))
		cons_unavail = TRUE;
	}
	syscons_unlock();
	DPRINTF(5, ("\n"));
	lwkt_reltoken(&tty_token);
	return 0;
    }

    case VT_GETMODE:    	/* get screen switcher mode */
	bcopy(&scp->smode, data, sizeof(struct vt_mode));
	lwkt_reltoken(&tty_token);
	return 0;

    case VT_RELDISP:    	/* screen switcher ioctl */
	/*
	 * This must be the current vty which is in the VT_PROCESS
	 * switching mode...
	 */
	syscons_lock();
	if ((scp != sc->cur_scp) || (scp->smode.mode != VT_PROCESS)) {
	    syscons_unlock();
	    lwkt_reltoken(&tty_token);
	    return EINVAL;
	}
	/* ...and this process is controlling it. */
	if (scp->proc != curproc) {
	    syscons_unlock();
	    lwkt_reltoken(&tty_token);
	    return EPERM;
	}
	error = EINVAL;
	switch(*(int *)data) {
	case VT_FALSE:  	/* user refuses to release screen, abort */
	    if ((error = finish_vt_rel(scp, FALSE)) == 0)
		DPRINTF(5, ("sc%d: VT_FALSE\n", sc->unit));
	    break;
	case VT_TRUE:   	/* user has released screen, go on */
	    if ((error = finish_vt_rel(scp, TRUE)) == 0)
		DPRINTF(5, ("sc%d: VT_TRUE\n", sc->unit));
	    break;
	case VT_ACKACQ: 	/* acquire acknowledged, switch completed */
	    if ((error = finish_vt_acq(scp)) == 0)
		DPRINTF(5, ("sc%d: VT_ACKACQ\n", sc->unit));
	    break;
	default:
	    break;
	}
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return error;

    case VT_OPENQRY:    	/* return free virtual console */
	for (i = sc->first_vty; i < sc->first_vty + sc->vtys; i++) {
	    tp = VIRTUAL_TTY(sc, i);
	    if (!ISTTYOPEN(tp)) {
		*(int *)data = i + 1;
		lwkt_reltoken(&tty_token);
		return 0;
	    }
	}
	lwkt_reltoken(&tty_token);
	return EINVAL;

    case VT_ACTIVATE:   	/* switch to screen *data */
	i = (*(int *)data == 0) ? scp->index : (*(int *)data - 1);
	syscons_lock();
	sc_clean_up(sc->cur_scp);
	error = sc_switch_scr(sc, i);
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return error;

    case VT_WAITACTIVE: 	/* wait for switch to occur */
	i = (*(int *)data == 0) ? scp->index : (*(int *)data - 1);
	if ((i < sc->first_vty) || (i >= sc->first_vty + sc->vtys)) {
	    lwkt_reltoken(&tty_token);
	    return EINVAL;
	}
	syscons_lock();
	error = sc_clean_up(sc->cur_scp);
	syscons_unlock();
	if (error) {
	    lwkt_reltoken(&tty_token);
	    return error;
	}

	/*
	 * scp might be NULL, we aren't sure why.  Check for NULL.
	 *
	 * http://bugs.dragonflybsd.org/issues/2481
	 */
	scp = SC_STAT(SC_DEV(sc, i));
	if (scp == NULL || scp == scp->sc->cur_scp) {
	    lwkt_reltoken(&tty_token);
	    return 0;
	}
	error = tsleep((caddr_t)&scp->smode, PCATCH, "waitvt", 0);
	/* May return ERESTART */
	lwkt_reltoken(&tty_token);
	return error;

    case VT_GETACTIVE:		/* get active vty # */
	*(int *)data = sc->cur_scp->index + 1;
	lwkt_reltoken(&tty_token);
	return 0;

    case VT_GETINDEX:		/* get this vty # */
	*(int *)data = scp->index + 1;
	lwkt_reltoken(&tty_token);
	return 0;

    case VT_LOCKSWITCH:		/* prevent vty switching */
	syscons_lock();
	if ((*(int *)data) & 0x01)
	    sc->flags |= SC_SCRN_VTYLOCK;
	else
	    sc->flags &= ~SC_SCRN_VTYLOCK;
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return 0;

    case KDENABIO:      	/* allow io operations */
	error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
	if (error != 0) {
	    lwkt_reltoken(&tty_token);
	    return error;
	}
	if (securelevel > 0) {
	    lwkt_reltoken(&tty_token);
	    return EPERM;
	}
#if defined(__i386__)
	curthread->td_lwp->lwp_md.md_regs->tf_eflags |= PSL_IOPL;
#elif defined(__x86_64__)
	curthread->td_lwp->lwp_md.md_regs->tf_rflags |= PSL_IOPL;
#endif
	lwkt_reltoken(&tty_token);
	return 0;

    case KDDISABIO:     	/* disallow io operations (default) */
#if defined(__i386__)
	curthread->td_lwp->lwp_md.md_regs->tf_eflags &= ~PSL_IOPL;
#elif defined(__x86_64__)
	curthread->td_lwp->lwp_md.md_regs->tf_rflags &= ~PSL_IOPL;
#endif
        lwkt_reltoken(&tty_token);
	return 0;

    case KDSKBSTATE:    	/* set keyboard state (locks) */
	if (*(int *)data & ~LOCK_MASK) {
	    lwkt_reltoken(&tty_token);
	    return EINVAL;
	}
	syscons_lock();
	scp->status &= ~LOCK_MASK;
	scp->status |= *(int *)data;
	syscons_unlock();
	if (scp == sc->cur_scp)
	    update_kbd_state(scp, scp->status, LOCK_MASK, FALSE);
	lwkt_reltoken(&tty_token);
	return 0;

    case KDGKBSTATE:    	/* get keyboard state (locks) */
	if (scp == sc->cur_scp)
	    save_kbd_state(scp, FALSE);
	*(int *)data = scp->status & LOCK_MASK;
	lwkt_reltoken(&tty_token);
	return 0;

    case KDGETREPEAT:      	/* get keyboard repeat & delay rates */
    case KDSETREPEAT:      	/* set keyboard repeat & delay rates (new) */
	error = kbd_ioctl(sc->kbd, cmd, data);
	if (error == ENOIOCTL)
	    error = ENODEV;
	lwkt_reltoken(&tty_token);
	return error;

    case KDSETRAD:      	/* set keyboard repeat & delay rates (old) */
	if (*(int *)data & ~0x7f) {
	    lwkt_reltoken(&tty_token);
	    return EINVAL;
	}
	error = kbd_ioctl(sc->kbd, cmd, data);
	if (error == ENOIOCTL)
	    error = ENODEV;
	lwkt_reltoken(&tty_token);
	return error;

    case KDSKBMODE:     	/* set keyboard mode */
	switch (*(int *)data) {
	case K_XLATE:   	/* switch to XLT ascii mode */
	case K_RAW: 		/* switch to RAW scancode mode */
	case K_CODE: 		/* switch to CODE mode */
	    scp->kbd_mode = *(int *)data;
	    if (scp == sc->cur_scp)
		kbd_ioctl(sc->kbd, cmd, data);
            lwkt_reltoken(&tty_token);
	    return 0;
	default:
	    lwkt_reltoken(&tty_token);
	    return EINVAL;
	}
	/* NOT REACHED */

    case KDGKBMODE:     	/* get keyboard mode */
	*(int *)data = scp->kbd_mode;
	lwkt_reltoken(&tty_token);
	return 0;

    case KDGKBINFO:
	error = kbd_ioctl(sc->kbd, cmd, data);
	if (error == ENOIOCTL)
	    error = ENODEV;
	lwkt_reltoken(&tty_token);
	return error;

    case KDMKTONE:      	/* sound the bell */
	syscons_lock();
	if (*(int*)data)
	    sc_bell(scp, (*(int*)data)&0xffff,
		    (((*(int*)data)>>16)&0xffff)*hz/1000);
	else
	    sc_bell(scp, scp->bell_pitch, scp->bell_duration);
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return 0;

    case KIOCSOUND:     	/* make tone (*data) hz */
	syscons_lock();
	if (scp == sc->cur_scp) {
	    if (*(int *)data) {
		error = sc_tone(*(int *)data);
	    } else {
		error = sc_tone(0);
	    }
	} else {
	    error = 0;
	}
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return error;

    case KDGKBTYPE:     	/* get keyboard type */
	error = kbd_ioctl(sc->kbd, cmd, data);
	if (error == ENOIOCTL) {
	    /* always return something? XXX */
	    *(int *)data = 0;
	}
	lwkt_reltoken(&tty_token);
	return 0;

    case KDSETLED:      	/* set keyboard LED status */
	if (*(int *)data & ~LED_MASK) {	/* FIXME: LOCK_MASK? */
	    lwkt_reltoken(&tty_token);
	    return EINVAL;
	}
	syscons_lock();
	scp->status &= ~LED_MASK;
	scp->status |= *(int *)data;
	syscons_unlock();
	if (scp == sc->cur_scp)
	    update_kbd_leds(scp, scp->status);
	lwkt_reltoken(&tty_token);
	return 0;

    case KDGETLED:      	/* get keyboard LED status */
	if (scp == sc->cur_scp)
	    save_kbd_state(scp, FALSE);
	*(int *)data = scp->status & LED_MASK;
	lwkt_reltoken(&tty_token);
	return 0;

	case KBADDKBD:              /* add/remove keyboard to/from mux */
	case KBRELKBD:
		error = kbd_ioctl(sc->kbd, cmd, data);
		if (error == ENOIOCTL)
			error = ENODEV;
		lwkt_reltoken(&tty_token);
		return error;

    case CONS_SETKBD: 		/* set the new keyboard */
	{
	    keyboard_t *newkbd;

	    newkbd = kbd_get_keyboard(*(int *)data);
	    if (newkbd == NULL) {
		lwkt_reltoken(&tty_token);
		return EINVAL;
	    }
	    error = 0;
	    if (sc->kbd != newkbd) {
		i = kbd_allocate(newkbd->kb_name, newkbd->kb_unit,
				 (void *)&sc->keyboard, sckbdevent, sc);
		/* i == newkbd->kb_index */
		if (i >= 0) {
		    if (sc->kbd != NULL) {
			save_kbd_state(sc->cur_scp, FALSE);
			kbd_release(sc->kbd, (void *)&sc->keyboard);
		    }
		    syscons_lock();
		    sc->kbd = kbd_get_keyboard(i); /* sc->kbd == newkbd */
		    sc->keyboard = i;
		    syscons_unlock();
		    kbd_ioctl(sc->kbd, KDSKBMODE,
			      (caddr_t)&sc->cur_scp->kbd_mode);
		    update_kbd_state(sc->cur_scp, sc->cur_scp->status,
			LOCK_MASK, FALSE);
		} else {
		    error = EPERM;	/* XXX */
		}
	    }
	    lwkt_reltoken(&tty_token);
	    return error;
	}

    case CONS_RELKBD: 		/* release the current keyboard */
	error = 0;
	if (sc->kbd != NULL) {
	    save_kbd_state(sc->cur_scp, FALSE);
	    error = kbd_release(sc->kbd, (void *)&sc->keyboard);
	    if (error == 0) {
		syscons_lock();
		sc->kbd = NULL;
		sc->keyboard = -1;
		syscons_unlock();
	    }
	}
	lwkt_reltoken(&tty_token);
	return error;

    case CONS_GETTERM:		/* get the current terminal emulator info */
	{
	    sc_term_sw_t *sw;

	    if (((term_info_t *)data)->ti_index == 0) {
		sw = scp->tsw;
	    } else {
		sw = sc_term_match_by_number(((term_info_t *)data)->ti_index);
	    }
	    if (sw != NULL) {
		strncpy(((term_info_t *)data)->ti_name, sw->te_name, 
			sizeof(((term_info_t *)data)->ti_name));
		strncpy(((term_info_t *)data)->ti_desc, sw->te_desc, 
			sizeof(((term_info_t *)data)->ti_desc));
		((term_info_t *)data)->ti_flags = 0;
		lwkt_reltoken(&tty_token);
		return 0;
	    } else {
		((term_info_t *)data)->ti_name[0] = '\0';
		((term_info_t *)data)->ti_desc[0] = '\0';
		((term_info_t *)data)->ti_flags = 0;
		lwkt_reltoken(&tty_token);
		return EINVAL;
	    }
	}

    case CONS_SETTERM:		/* set the current terminal emulator */
	syscons_lock();
	error = sc_init_emulator(scp, ((term_info_t *)data)->ti_name);
	/* FIXME: what if scp == sc_console! XXX */
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return error;

    case GIO_SCRNMAP:   	/* get output translation table */
	bcopy(&sc->scr_map, data, sizeof(sc->scr_map));
	lwkt_reltoken(&tty_token);
	return 0;

    case PIO_SCRNMAP:   	/* set output translation table */
	bcopy(data, &sc->scr_map, sizeof(sc->scr_map));
	for (i=0; i<sizeof(sc->scr_map); i++) {
	    sc->scr_rmap[sc->scr_map[i]] = i;
	}
	lwkt_reltoken(&tty_token);
	return 0;

    case GIO_KEYMAP:		/* get keyboard translation table */
    case PIO_KEYMAP:		/* set keyboard translation table */
    case GIO_DEADKEYMAP:	/* get accent key translation table */
    case PIO_DEADKEYMAP:	/* set accent key translation table */
    case GETFKEY:		/* get function key string */
    case SETFKEY:		/* set function key string */
	error = kbd_ioctl(sc->kbd, cmd, data);
	if (error == ENOIOCTL)
	    error = ENODEV;
	lwkt_reltoken(&tty_token);
	return error;

#ifndef SC_NO_FONT_LOADING

    case PIO_FONT8x8:   	/* set 8x8 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags)) {
	    lwkt_reltoken(&tty_token);
	    return ENXIO;
	}
	syscons_lock();
	bcopy(data, sc->font_8, 8*256);
	sc->fonts_loaded |= FONT_8;
	/*
	 * FONT KLUDGE
	 * Always use the font page #0. XXX
	 * Don't load if the current font size is not 8x8.
	 */
	if (ISTEXTSC(sc->cur_scp) && (sc->cur_scp->font_size < 14))
	    sc_load_font(sc->cur_scp, 0, 8, sc->font_8, 0, 256);
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return 0;

    case GIO_FONT8x8:   	/* get 8x8 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags)) {
	    lwkt_reltoken(&tty_token);
	    return ENXIO;
	}
	if (sc->fonts_loaded & FONT_8) {
	    bcopy(sc->font_8, data, 8*256);
	    lwkt_reltoken(&tty_token);
	    return 0;
	}
	else {
	    lwkt_reltoken(&tty_token);
	    return ENXIO;
	}

    case PIO_FONT8x14:  	/* set 8x14 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags)) {
	    lwkt_reltoken(&tty_token);
	    return ENXIO;
	}
	syscons_lock();
	bcopy(data, sc->font_14, 14*256);
	sc->fonts_loaded |= FONT_14;
	/*
	 * FONT KLUDGE
	 * Always use the font page #0. XXX
	 * Don't load if the current font size is not 8x14.
	 */
	if (ISTEXTSC(sc->cur_scp)
	    && (sc->cur_scp->font_size >= 14)
	    && (sc->cur_scp->font_size < 16)) {
	    sc_load_font(sc->cur_scp, 0, 14, sc->font_14, 0, 256);
	}
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return 0;

    case GIO_FONT8x14:  	/* get 8x14 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags)) {
	    lwkt_reltoken(&tty_token);
	    return ENXIO;
	}
	if (sc->fonts_loaded & FONT_14) {
	    bcopy(sc->font_14, data, 14*256);
	    lwkt_reltoken(&tty_token);
	    return 0;
	}
	else {
	    lwkt_reltoken(&tty_token);
	    return ENXIO;
        }

    case PIO_FONT8x16:  	/* set 8x16 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags)) {
	    lwkt_reltoken(&tty_token);
	    return ENXIO;
	}
	syscons_lock();
	bcopy(data, sc->font_16, 16*256);
	sc->fonts_loaded |= FONT_16;
	/*
	 * FONT KLUDGE
	 * Always use the font page #0. XXX
	 * Don't load if the current font size is not 8x16.
	 */
	if (ISTEXTSC(sc->cur_scp) && (sc->cur_scp->font_size >= 16))
	    sc_load_font(sc->cur_scp, 0, 16, sc->font_16, 0, 256);
	syscons_unlock();
	lwkt_reltoken(&tty_token);
	return 0;

    case GIO_FONT8x16:  	/* get 8x16 dot font */
	if (!ISFONTAVAIL(sc->adp->va_flags)) {
	    lwkt_reltoken(&tty_token);
	    return ENXIO;
	}
	if (sc->fonts_loaded & FONT_16) {
	    bcopy(sc->font_16, data, 16*256);
	    lwkt_reltoken(&tty_token);
	    return 0;
	}
	else {
	    lwkt_reltoken(&tty_token);
	    return ENXIO;
        }

#endif /* SC_NO_FONT_LOADING */

    default:
	break;
    }

    error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, ap->a_cred);
    if (error != ENOIOCTL) {
        lwkt_reltoken(&tty_token);
	return(error);
    }
    error = ttioctl(tp, cmd, data, flag);
    if (error != ENOIOCTL) {
        lwkt_reltoken(&tty_token);
	return(error);
    }
    lwkt_reltoken(&tty_token);
    return(ENOTTY);
}

static void
scstart(struct tty *tp)
{
    struct clist *rbp;
    int len;
    u_char buf[PCBURST];
    scr_stat *scp = SC_STAT(tp->t_dev);

    syscons_lock();
    if (scp->status & SLKED ||
	(scp == scp->sc->cur_scp && scp->sc->blink_in_progress))
    {
	syscons_unlock();
	return;
    }
    if (!(tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP))) {
	tp->t_state |= TS_BUSY;
	rbp = &tp->t_outq;
	while (rbp->c_cc) {
	    len = q_to_b(rbp, buf, PCBURST);
	    sc_puts(scp, buf, len);
	}
	tp->t_state &= ~TS_BUSY;
	syscons_unlock();
	ttwwakeup(tp);
    } else {
	syscons_unlock();
    }
}

static void
sccnprobe(struct consdev *cp)
{
    int unit;
    int flags;

    cp->cn_pri = sc_get_cons_priority(&unit, &flags);

    /* a video card is always required */
    if (!scvidprobe(unit, flags, TRUE))
	cp->cn_pri = CN_DEAD;

    /* syscons will become console even when there is no keyboard */
    sckbdprobe(unit, flags, TRUE);

    if (cp->cn_pri == CN_DEAD) {
	return;
    }

    /* initialize required fields */
    cp->cn_probegood = 1;
}

static void
sccninit(struct consdev *cp)
{
    int unit;
    int flags;

    sc_get_cons_priority(&unit, &flags);
    scinit(unit, flags | SC_KERNEL_CONSOLE);
    sc_console_unit = unit;
    sc_console = sc_get_softc(unit, SC_KERNEL_CONSOLE)->console_scp;
}

static void
sccninit_fini(struct consdev *cp)
{
	if (cctl_dev == NULL)
		kprintf("sccninit_fini: WARNING: cctl_dev is NULL!\n");
	cp->cn_dev = cctl_dev;
}

static void
sccnterm(struct consdev *cp)
{
    /* we are not the kernel console any more, release everything */

    if (sc_console_unit < 0)
	return;			/* shouldn't happen */

#if 0 /* XXX */
    syscons_lock();
    sc_clear_screen(sc_console);
    sccnupdate(sc_console);
    syscons_unlock();
#endif
    scterm(sc_console_unit, SC_KERNEL_CONSOLE);
    sc_console_unit = -1;
    sc_console = NULL;
}

/*
 * Console path - cannot block!
 */
static void
sccnputc(void *private, int c)
{
    u_char buf[1];
    scr_stat *scp = sc_console;
    void *save;
#ifndef SC_NO_HISTORY
#if 0
    struct tty *tp;
#endif
#endif /* !SC_NO_HISTORY */

    /* assert(sc_console != NULL) */

    syscons_lock();
#ifndef SC_NO_HISTORY
    if (scp == scp->sc->cur_scp && scp->status & SLKED) {
	scp->status &= ~SLKED;
#if 0
	/* This can block, illegal in the console path */
	update_kbd_state(scp, scp->status, SLKED, TRUE);
#endif
	if (scp->status & BUFFER_SAVED) {
	    if (!sc_hist_restore(scp))
		sc_remove_cutmarking(scp);
	    scp->status &= ~BUFFER_SAVED;
	    scp->status |= CURSOR_ENABLED;
	    sc_draw_cursor_image(scp);
	}
#if 0
	tp = VIRTUAL_TTY(scp->sc, scp->index);
	/* This can block, illegal in the console path */
	if (ISTTYOPEN(tp)) {
	    scstart(tp);
	}
#endif
    }
#endif /* !SC_NO_HISTORY */

    save = scp->ts;
    if (kernel_console_ts != NULL)
	scp->ts = kernel_console_ts;
    buf[0] = c;
    sc_puts(scp, buf, 1);
    scp->ts = save;

    sccnupdate(scp);
    syscons_unlock();
}

/*
 * Console path - cannot block!
 */
static int
sccngetc(void *private)
{
    return sccngetch(0);
}

/*
 * Console path - cannot block!
 */
static int
sccncheckc(void *private)
{
    return sccngetch(SCGETC_NONBLOCK);
}

static void
sccndbctl(void *private, int on)
{
    /* assert(sc_console_unit >= 0) */
    /* try to switch to the kernel console screen */
    if (on && debugger == 0) {
	/*
	 * TRY to make sure the screen saver is stopped, 
	 * and the screen is updated before switching to 
	 * the vty0.
	 */
	scrn_timer(NULL);
	if (!cold
	    && sc_console->sc->cur_scp->smode.mode == VT_AUTO
	    && sc_console->smode.mode == VT_AUTO) {
	    sc_console->sc->cur_scp->status |= MOUSE_HIDDEN;
	    syscons_lock();
	    sc_switch_scr(sc_console->sc, sc_console->index);
	    syscons_unlock();
	}
    }
    if (on)
	++debugger;
    else
	--debugger;
}

/*
 * Console path - cannot block!
 */
static int
sccngetch(int flags)
{
    static struct fkeytab fkey;
    static int fkeycp;
    scr_stat *scp;
    u_char *p;
    int cur_mode;
    int c;

    syscons_lock();
    /* assert(sc_console != NULL) */

    /* 
     * Stop the screen saver and update the screen if necessary.
     * What if we have been running in the screen saver code... XXX
     */
    sc_touch_scrn_saver();
    scp = sc_console->sc->cur_scp;	/* XXX */
    sccnupdate(scp);
    syscons_unlock();

    if (fkeycp < fkey.len) {
	return fkey.str[fkeycp++];
    }

    if (scp->sc->kbd == NULL) {
	return -1;
    }

    /* 
     * Make sure the keyboard is accessible even when the kbd device
     * driver is disabled.
     */
    crit_enter();
    kbd_enable(scp->sc->kbd);

    /* we shall always use the keyboard in the XLATE mode here */
    cur_mode = scp->kbd_mode;
    scp->kbd_mode = K_XLATE;
    kbd_ioctl(scp->sc->kbd, KDSKBMODE, (caddr_t)&scp->kbd_mode);

    kbd_poll(scp->sc->kbd, TRUE);
    c = scgetc(scp->sc, SCGETC_CN | flags);
    kbd_poll(scp->sc->kbd, FALSE);

    scp->kbd_mode = cur_mode;
    kbd_ioctl(scp->sc->kbd, KDSKBMODE, (caddr_t)&scp->kbd_mode);
    kbd_disable(scp->sc->kbd);
    crit_exit();

    switch (KEYFLAGS(c)) {
    case 0:	/* normal char */
	return KEYCHAR(c);
    case FKEY:	/* function key */
	p = kbd_get_fkeystr(scp->sc->kbd, KEYCHAR(c), (size_t *)&fkeycp);
	fkey.len = fkeycp;
	if ((p != NULL) && (fkey.len > 0)) {
	    bcopy(p, fkey.str, fkey.len);
	    fkeycp = 1;
	    return fkey.str[0];
	}
	return c;	/* XXX */
    case NOKEY:
    case ERRKEY:
    default:
	return -1;
    }
    /* NOT REACHED */
}

static void
sccnupdate(scr_stat *scp)
{
    /* this is a cut-down version of scrn_timer()... */

    if (scp->sc->font_loading_in_progress || scp->sc->videoio_in_progress) {
	return;
    }

    if (debugger > 0 || panicstr || shutdown_in_progress) {
	sc_touch_scrn_saver();
    } else if (scp != scp->sc->cur_scp) {
	return;
    }

    if (!run_scrn_saver)
	scp->sc->flags &= ~SC_SCRN_IDLE;
#if NSPLASH > 0
    /*
     * This is a hard path, we cannot call stop_scrn_saver() here.
     */
    if ((saver_mode != CONS_LKM_SAVER) || !(scp->sc->flags & SC_SCRN_IDLE))
	if (scp->sc->flags & SC_SCRN_BLANKED) {
	    sc_touch_scrn_saver();
            /*stop_scrn_saver(scp->sc, current_saver);*/
	}
#endif /* NSPLASH */

    if (scp != scp->sc->cur_scp || scp->sc->blink_in_progress
	|| scp->sc->switch_in_progress) {
	return;
    }
    /*
     * FIXME: unlike scrn_timer(), we call scrn_update() from here even
     * when write_in_progress is non-zero.  XXX
     */

    if (!ISGRAPHSC(scp) && !(scp->sc->flags & SC_SCRN_BLANKED))
	scrn_update(scp, TRUE);
}

static void
scrn_timer(void *arg)
{
    static int kbd_interval = 0;
    struct timeval tv;
    sc_softc_t *sc;
    scr_stat *scp;
    int again;

    /*
     * Setup depending on who called us
     */
    again = (arg != NULL);
    if (arg != NULL) {
	sc = (sc_softc_t *)arg;
    } else if (sc_console != NULL) {
	sc = sc_console->sc;
    } else {
	return;
    }

    /*
     * Don't do anything when we are performing some I/O operations.
     * (These are initiated by the frontend?)
     */
    if (sc->font_loading_in_progress || sc->videoio_in_progress) {
	if (again)
	    callout_reset(&sc->scrn_timer_ch, hz / 10, scrn_timer, sc);
	return;
    }

    /*
     * Try to allocate a keyboard automatically
     */
    if ((sc->kbd == NULL) && (sc->config & SC_AUTODETECT_KBD)) {
	if (++kbd_interval >= 25) {
	    sc->keyboard = sc_allocate_keyboard(sc, -1);
	    if (sc->keyboard >= 0) {
		sc->kbd = kbd_get_keyboard(sc->keyboard);
		kbd_ioctl(sc->kbd, KDSKBMODE,
			  (caddr_t)&sc->cur_scp->kbd_mode);
		update_kbd_state(sc->cur_scp, sc->cur_scp->status,
		    LOCK_MASK, FALSE);
	    }
	    kbd_interval = 0;
	}
    }

    /*
     * Should we stop the screen saver?  We need the syscons_lock
     * for most of this stuff.
     */
    getmicrouptime(&tv);

    if (syscons_lock_nonblock() != 0) {
	/* failed to get the lock */
	if (again)
	    callout_reset(&sc->scrn_timer_ch, hz / 10, scrn_timer, sc);
	return;
    }
    /* successful lock */

    if (debugger > 0 || panicstr || shutdown_in_progress)
	sc_touch_scrn_saver();
    if (run_scrn_saver) {
	if (tv.tv_sec > sc->scrn_time_stamp + scrn_blank_time)
	    sc->flags |= SC_SCRN_IDLE;
	else
	    sc->flags &= ~SC_SCRN_IDLE;
    } else {
	sc->scrn_time_stamp = tv.tv_sec;
	sc->flags &= ~SC_SCRN_IDLE;
	if (scrn_blank_time > 0)
	    run_scrn_saver = TRUE;
    }
#if NSPLASH > 0
    if ((saver_mode != CONS_LKM_SAVER) || !(sc->flags & SC_SCRN_IDLE))
	if (sc->flags & SC_SCRN_BLANKED)
            stop_scrn_saver(sc, current_saver);
#endif /* NSPLASH */

    /* should we just return ? */
    if (sc->blink_in_progress || sc->switch_in_progress ||
	sc->write_in_progress)
    {
	syscons_unlock();
	if (again)
	    callout_reset(&sc->scrn_timer_ch, hz / 10, scrn_timer, sc);
	return;
    }

    /* Update the screen */
    scp = sc->cur_scp;		/* cur_scp may have changed... */
    if (!ISGRAPHSC(scp) && !(sc->flags & SC_SCRN_BLANKED))
	scrn_update(scp, TRUE);

#if NSPLASH > 0
    /* should we activate the screen saver? */
    if ((saver_mode == CONS_LKM_SAVER) && (sc->flags & SC_SCRN_IDLE))
	if (!ISGRAPHSC(scp) || (sc->flags & SC_SCRN_BLANKED))
	    (*current_saver)(sc, TRUE);
#endif /* NSPLASH */

    syscons_unlock();
    if (again)
	callout_reset(&sc->scrn_timer_ch, hz / 25, scrn_timer, sc);
}

static int
and_region(int *s1, int *e1, int s2, int e2)
{
    if (*e1 < s2 || e2 < *s1)
	return FALSE;
    *s1 = imax(*s1, s2);
    *e1 = imin(*e1, e2);
    return TRUE;
}

static void 
scrn_update(scr_stat *scp, int show_cursor)
{
    int start;
    int end;
    int s;
    int e;

    /* assert(scp == scp->sc->cur_scp) */

    ++scp->sc->videoio_in_progress;

#ifndef SC_NO_CUTPASTE
    /* remove the previous mouse pointer image if necessary */
    if (scp->status & MOUSE_VISIBLE) {
	s = scp->mouse_pos;
	e = scp->mouse_pos + scp->xsize + 1;
	if ((scp->status & (MOUSE_MOVED | MOUSE_HIDDEN))
	    || and_region(&s, &e, scp->start, scp->end)
	    || ((scp->status & CURSOR_ENABLED) && 
		(scp->cursor_pos != scp->cursor_oldpos) &&
		(and_region(&s, &e, scp->cursor_pos, scp->cursor_pos)
		 || and_region(&s, &e, scp->cursor_oldpos, scp->cursor_oldpos)))) {
	    sc_remove_mouse_image(scp);
	    if (scp->end >= scp->xsize*scp->ysize)
		scp->end = scp->xsize*scp->ysize - 1;
	}
    }
#endif /* !SC_NO_CUTPASTE */

#if 1
    /* debug: XXX */
    if (scp->end >= scp->xsize*scp->ysize) {
	kprintf("scrn_update(): scp->end %d > size_of_screen!!\n", scp->end);
	scp->end = scp->xsize*scp->ysize - 1;
    }
    if (scp->start < 0) {
	kprintf("scrn_update(): scp->start %d < 0\n", scp->start);
	scp->start = 0;
    }
#endif

    /* update screen image */
    if (scp->start <= scp->end)  {
	if (scp->mouse_cut_end >= 0) {
	    /* there is a marked region for cut & paste */
	    if (scp->mouse_cut_start <= scp->mouse_cut_end) {
		start = scp->mouse_cut_start;
		end = scp->mouse_cut_end;
	    } else {
		start = scp->mouse_cut_end;
		end = scp->mouse_cut_start - 1;
	    }
	    s = start;
	    e = end;
	    /* does the cut-mark region overlap with the update region? */
	    if (and_region(&s, &e, scp->start, scp->end)) {
		(*scp->rndr->draw)(scp, s, e - s + 1, TRUE);
		s = 0;
		e = start - 1;
		if (and_region(&s, &e, scp->start, scp->end))
		    (*scp->rndr->draw)(scp, s, e - s + 1, FALSE);
		s = end + 1;
		e = scp->xsize*scp->ysize - 1;
		if (and_region(&s, &e, scp->start, scp->end))
		    (*scp->rndr->draw)(scp, s, e - s + 1, FALSE);
	    } else {
		(*scp->rndr->draw)(scp, scp->start,
				   scp->end - scp->start + 1, FALSE);
	    }
	} else {
	    (*scp->rndr->draw)(scp, scp->start,
			       scp->end - scp->start + 1, FALSE);
	}
    }

    /* we are not to show the cursor and the mouse pointer... */
    if (!show_cursor) {
        scp->end = 0;
        scp->start = scp->xsize*scp->ysize - 1;
	--scp->sc->videoio_in_progress;
	return;
    }

    /* update cursor image */
    if (scp->status & CURSOR_ENABLED) {
	s = scp->start;
	e = scp->end;
        /* did cursor move since last time ? */
        if (scp->cursor_pos != scp->cursor_oldpos) {
            /* do we need to remove old cursor image ? */
            if (!and_region(&s, &e, scp->cursor_oldpos, scp->cursor_oldpos))
                sc_remove_cursor_image(scp);
            sc_draw_cursor_image(scp);
        } else {
            if (and_region(&s, &e, scp->cursor_pos, scp->cursor_pos))
		/* cursor didn't move, but has been overwritten */
		sc_draw_cursor_image(scp);
	    else if (scp->sc->flags & SC_BLINK_CURSOR)
		/* if it's a blinking cursor, update it */
		(*scp->rndr->blink_cursor)(scp, scp->cursor_pos,
					   sc_inside_cutmark(scp,
					       scp->cursor_pos));
        }
    }

#ifndef SC_NO_CUTPASTE
    /* update "pseudo" mouse pointer image */
    if (scp->sc->flags & SC_MOUSE_ENABLED) {
	if (!(scp->status & (MOUSE_VISIBLE | MOUSE_HIDDEN))) {
	    scp->status &= ~MOUSE_MOVED;
	    sc_draw_mouse_image(scp);
	}
    }
#endif /* SC_NO_CUTPASTE */

    scp->end = 0;
    scp->start = scp->xsize*scp->ysize - 1;

    --scp->sc->videoio_in_progress;
}

#if NSPLASH > 0
static int
scsplash_callback(int event, void *arg)
{
    sc_softc_t *sc;
    int error;

    sc = (sc_softc_t *)arg;

    switch (event) {
    case SPLASH_INIT:
	if (add_scrn_saver(scsplash_saver) == 0) {
	    sc->flags &= ~SC_SAVER_FAILED;
	    run_scrn_saver = TRUE;
	    if (cold && !(boothowto & (RB_VERBOSE | RB_CONFIG))) {
		scsplash_stick(TRUE);
		(*current_saver)(sc, TRUE);
	    }
	}
	return 0;

    case SPLASH_TERM:
	if (current_saver == scsplash_saver) {
	    scsplash_stick(FALSE);
	    error = remove_scrn_saver(scsplash_saver);
	    if (error) {
		return error;
            }
	}
	return 0;

    default:
	return EINVAL;
    }
}

static void
scsplash_saver(sc_softc_t *sc, int show)
{
    static int busy = FALSE;
    scr_stat *scp;

    if (busy)
	return;
    busy = TRUE;

    scp = sc->cur_scp;
    if (show) {
	if (!(sc->flags & SC_SAVER_FAILED)) {
	    if (!(sc->flags & SC_SCRN_BLANKED))
		set_scrn_saver_mode(scp, -1, NULL, 0);
	    switch (splash(sc->adp, TRUE)) {
	    case 0:		/* succeeded */
		break;
	    case EAGAIN:	/* try later */
		restore_scrn_saver_mode(scp, FALSE);
		sc_touch_scrn_saver();		/* XXX */
		break;
	    default:
		sc->flags |= SC_SAVER_FAILED;
		scsplash_stick(FALSE);
		restore_scrn_saver_mode(scp, TRUE);
		kprintf("scsplash_saver(): failed to put up the image\n");
		break;
	    }
	}
    } else if (!sticky_splash) {
	if ((sc->flags & SC_SCRN_BLANKED) && (splash(sc->adp, FALSE) == 0))
	    restore_scrn_saver_mode(scp, TRUE);
    }
    busy = FALSE;
}

static int
add_scrn_saver(void (*this_saver)(sc_softc_t *, int))
{
#if 0
    int error;

    if (current_saver != none_saver) {
	error = remove_scrn_saver(current_saver);
	if (error)
	    return error;
    }
#endif
    if (current_saver != none_saver) {
	return EBUSY;
    }

    run_scrn_saver = FALSE;
    saver_mode = CONS_LKM_SAVER;
    current_saver = this_saver;
    return 0;
}

static int
remove_scrn_saver(void (*this_saver)(sc_softc_t *, int))
{
    if (current_saver != this_saver)
	return EINVAL;

#if 0
    /*
     * In order to prevent `current_saver' from being called by
     * the timeout routine `scrn_timer()' while we manipulate 
     * the saver list, we shall set `current_saver' to `none_saver' 
     * before stopping the current saver, rather than blocking by `splXX()'.
     */
    current_saver = none_saver;
    if (scrn_blanked)
        stop_scrn_saver(this_saver);
#endif
    /* unblank all blanked screens */
    wait_scrn_saver_stop(NULL);
    if (scrn_blanked) {
	return EBUSY;
    }

    current_saver = none_saver;
    return 0;
}

static int
set_scrn_saver_mode(scr_stat *scp, int mode, u_char *pal, int border)
{

    /* assert(scp == scp->sc->cur_scp) */
    crit_enter();
    if (!ISGRAPHSC(scp))
	sc_remove_cursor_image(scp);
    scp->splash_save_mode = scp->mode;
    scp->splash_save_status = scp->status & (GRAPHICS_MODE | PIXEL_MODE);
    scp->status &= ~(GRAPHICS_MODE | PIXEL_MODE);
    scp->status |= (UNKNOWN_MODE | SAVER_RUNNING);
    scp->sc->flags |= SC_SCRN_BLANKED;
    ++scrn_blanked;
    crit_exit();
    if (mode < 0) {
	return 0;
    }
    scp->mode = mode;
    if (set_mode(scp) == 0) {
	if (scp->sc->adp->va_info.vi_flags & V_INFO_GRAPHICS)
	    scp->status |= GRAPHICS_MODE;
#ifndef SC_NO_PALETTE_LOADING
	if (pal != NULL)
	    load_palette(scp->sc->adp, pal);
#endif
	sc_set_border(scp, border);
	return 0;
    } else {
	crit_enter();
	scp->mode = scp->splash_save_mode;
	scp->status &= ~(UNKNOWN_MODE | SAVER_RUNNING);
	scp->status |= scp->splash_save_status;
	crit_exit();
	return 1;
    }
    /* NOTREACHED */
}

static int
restore_scrn_saver_mode(scr_stat *scp, int changemode)
{
    int mode;
    int status;

    /* assert(scp == scp->sc->cur_scp) */
    crit_enter();
    mode = scp->mode;
    status = scp->status;
    scp->mode = scp->splash_save_mode;
    scp->status &= ~(UNKNOWN_MODE | SAVER_RUNNING);
    scp->status |= scp->splash_save_status;
    scp->sc->flags &= ~SC_SCRN_BLANKED;
    if (!changemode) {
	if (!ISGRAPHSC(scp))
	    sc_draw_cursor_image(scp);
	--scrn_blanked;
	crit_exit();
	return 0;
    }
    if (set_mode(scp) == 0) {
#ifndef SC_NO_PALETTE_LOADING
	load_palette(scp->sc->adp, scp->sc->palette);
#endif
	--scrn_blanked;
	crit_exit();
	return 0;
    } else {
	scp->mode = mode;
	scp->status = status;
	crit_exit();
	return 1;
    }
    /* NOTREACHED */
}

static void
stop_scrn_saver(sc_softc_t *sc, void (*saver)(sc_softc_t *, int))
{
    (*saver)(sc, FALSE);
    run_scrn_saver = FALSE;
    /* the screen saver may have chosen not to stop after all... */
    if (sc->flags & SC_SCRN_BLANKED) {
	return;
    }

    mark_all(sc->cur_scp);
    if (sc->delayed_next_scr)
	sc_switch_scr(sc, sc->delayed_next_scr - 1);
    wakeup((caddr_t)&scrn_blanked);
}

static int
wait_scrn_saver_stop(sc_softc_t *sc)
{
    int error = 0;

    while (scrn_blanked > 0) {
	run_scrn_saver = FALSE;
	if (sc && !(sc->flags & SC_SCRN_BLANKED)) {
	    error = 0;
	    break;
	}
	error = tsleep((caddr_t)&scrn_blanked, PCATCH, "scrsav", 0);
	/* May return ERESTART */
	if (error)
		break;
    }
    run_scrn_saver = FALSE;
    return error;
}
#endif /* NSPLASH */

void
sc_touch_scrn_saver(void)
{
    scsplash_stick(FALSE);
    run_scrn_saver = FALSE;
}

int
sc_switch_scr(sc_softc_t *sc, u_int next_scr)
{
    scr_stat *cur_scp;
    struct tty *tp;

    DPRINTF(5, ("sc0: sc_switch_scr() %d ", next_scr + 1));

    /* prevent switch if previously requested */
    if (sc->flags & SC_SCRN_VTYLOCK) {
	    sc_bell(sc->cur_scp, sc->cur_scp->bell_pitch,
		sc->cur_scp->bell_duration);
	    return EPERM;
    }

    /* delay switch if the screen is blanked or being updated */
    if ((sc->flags & SC_SCRN_BLANKED) || sc->write_in_progress
	|| sc->blink_in_progress || sc->videoio_in_progress) {
	sc->delayed_next_scr = next_scr + 1;
	sc_touch_scrn_saver();
	DPRINTF(5, ("switch delayed\n"));
	return 0;
    }

    cur_scp = sc->cur_scp;

    /*
     * we are in the middle of the vty switching process...
     *
     * This may be in the console path, be very careful.  pfindn() is
     * still going to use a spinlock but it no longer uses tokens so
     * we should be ok.
     */
    if (sc->switch_in_progress &&
	(cur_scp->smode.mode == VT_PROCESS) &&
	cur_scp->proc) {
	if (cur_scp->proc != pfindn(cur_scp->pid)) {
	    /* 
	     * The controlling process has died!!.  Do some clean up.
	     * NOTE:`cur_scp->proc' and `cur_scp->smode.mode' 
	     * are not reset here yet; they will be cleared later.
	     */
	    DPRINTF(5, ("cur_scp controlling process %d died, ", cur_scp->pid));
	    if (cur_scp->status & SWITCH_WAIT_REL) {
		/*
		 * Force the previous switch to finish, but return now 
		 * with error.
		 *
		 */
		DPRINTF(5, ("reset WAIT_REL, "));
		finish_vt_rel(cur_scp, TRUE);
		DPRINTF(5, ("finishing previous switch\n"));
		return EINVAL;
	    } else if (cur_scp->status & SWITCH_WAIT_ACQ) {
		/* let's assume screen switch has been completed. */
		DPRINTF(5, ("reset WAIT_ACQ, "));
		finish_vt_acq(cur_scp);
	    } else {
		/* 
	 	 * We are in between screen release and acquisition, and
		 * reached here via scgetc() or scrn_timer() which has 
		 * interrupted exchange_scr(). Don't do anything stupid.
		 */
		DPRINTF(5, ("waiting nothing, "));
	    }
	} else {
	    /*
	     * The controlling process is alive, but not responding... 
	     * It is either buggy or it may be just taking time.
	     * The following code is a gross kludge to cope with this
	     * problem for which there is no clean solution. XXX
	     */
	    if (cur_scp->status & SWITCH_WAIT_REL) {
		switch (sc->switch_in_progress++) {
		case 1:
		    break;
		case 2:
		    DPRINTF(5, ("sending relsig again, "));
		    signal_vt_rel(cur_scp);
		    break;
		case 3:
		    break;
		case 4:
		default:
		    /*
		     * Act as if the controlling program returned
		     * VT_FALSE.
		     *
		     */
		    DPRINTF(5, ("force reset WAIT_REL, "));
		    finish_vt_rel(cur_scp, FALSE);
		    DPRINTF(5, ("act as if VT_FALSE was seen\n"));
		    return EINVAL;
		}
	    } else if (cur_scp->status & SWITCH_WAIT_ACQ) {
		switch (sc->switch_in_progress++) {
		case 1:
		    break;
		case 2:
		    DPRINTF(5, ("sending acqsig again, "));
		    signal_vt_acq(cur_scp);
		    break;
		case 3:
		    break;
		case 4:
		default:
		     /* clear the flag and finish the previous switch */
		    DPRINTF(5, ("force reset WAIT_ACQ, "));
		    finish_vt_acq(cur_scp);
		    break;
		}
	    }
	}
    }

    /*
     * Return error if an invalid argument is given, or vty switch
     * is still in progress.
     */
    if ((next_scr < sc->first_vty) || (next_scr >= sc->first_vty + sc->vtys)
	|| sc->switch_in_progress) {
	sc_bell(cur_scp, bios_value.bell_pitch, BELL_DURATION);
	DPRINTF(5, ("error 1\n"));
	return EINVAL;
    }

    /*
     * Don't allow switching away from the graphics mode vty
     * if the switch mode is VT_AUTO, unless the next vty is the same 
     * as the current or the current vty has been closed (but showing).
     */
    tp = VIRTUAL_TTY(sc, cur_scp->index);
    if ((cur_scp->index != next_scr)
	&& ISTTYOPEN(tp)
	&& (cur_scp->smode.mode == VT_AUTO)
	&& ISGRAPHSC(cur_scp)) {
	sc_bell(cur_scp, bios_value.bell_pitch, BELL_DURATION);
	DPRINTF(5, ("error, graphics mode\n"));
	return EINVAL;
    }

    /*
     * Is the wanted vty open? Don't allow switching to a closed vty.
     * If we are in DDB, don't switch to a vty in the VT_PROCESS mode.
     * Note that we always allow the user to switch to the kernel 
     * console even if it is closed.
     */
    if ((sc_console == NULL) || (next_scr != sc_console->index)) {
	tp = VIRTUAL_TTY(sc, next_scr);
	if (!ISTTYOPEN(tp)) {
	    sc_bell(cur_scp, bios_value.bell_pitch, BELL_DURATION);
	    DPRINTF(5, ("error 2, requested vty isn't open!\n"));
	    return EINVAL;
	}
	if ((debugger > 0) && (SC_STAT(tp->t_dev)->smode.mode == VT_PROCESS)) {
	    DPRINTF(5, ("error 3, requested vty is in the VT_PROCESS mode\n"));
	    return EINVAL;
	}
    }

    /* this is the start of vty switching process... */
    ++sc->switch_in_progress;
    sc->delayed_next_scr = 0;
    sc->old_scp = cur_scp;
    sc->new_scp = SC_STAT(SC_DEV(sc, next_scr));
    if (sc->new_scp == sc->old_scp) {
	sc->switch_in_progress = 0;
	wakeup((caddr_t)&sc->new_scp->smode);
	DPRINTF(5, ("switch done (new == old)\n"));
	return 0;
    }

    /* has controlling process died? */
    vt_proc_alive(sc->old_scp);
    vt_proc_alive(sc->new_scp);

    /* wait for the controlling process to release the screen, if necessary */
    if (signal_vt_rel(sc->old_scp)) {
	return 0;
    }

    /* go set up the new vty screen */
    exchange_scr(sc);

    /* wake up processes waiting for this vty */
    wakeup((caddr_t)&sc->cur_scp->smode);

    /* wait for the controlling process to acknowledge, if necessary */
    if (signal_vt_acq(sc->cur_scp)) {
	return 0;
    }

    sc->switch_in_progress = 0;
    if (sc->unit == sc_console_unit)
	cons_unavail = FALSE;
    DPRINTF(5, ("switch done\n"));

    return 0;
}

static void
do_switch_scr(sc_softc_t *sc)
{
    lwkt_gettoken(&tty_token);
    vt_proc_alive(sc->new_scp);

    exchange_scr(sc);
    /* sc->cur_scp == sc->new_scp */
    wakeup((caddr_t)&sc->cur_scp->smode);

    /* wait for the controlling process to acknowledge, if necessary */
    if (!signal_vt_acq(sc->cur_scp)) {
	sc->switch_in_progress = 0;
	if (sc->unit == sc_console_unit)
	    cons_unavail = FALSE;
    }
    lwkt_reltoken(&tty_token);
}

static int
vt_proc_alive(scr_stat *scp)
{
    lwkt_gettoken(&tty_token);
    if (scp->proc) {
	if (scp->proc == pfindn(scp->pid)) {
	    lwkt_reltoken(&tty_token);
	    return TRUE;
	}
	scp->proc = NULL;
	scp->smode.mode = VT_AUTO;
	DPRINTF(5, ("vt controlling process %d died\n", scp->pid));
    }
    lwkt_reltoken(&tty_token);
    return FALSE;
}

static int
signal_vt_rel(scr_stat *scp)
{
    struct proc *p;

    lwkt_gettoken(&tty_token);
    if (scp->smode.mode != VT_PROCESS) {
        lwkt_reltoken(&tty_token);
	return FALSE;
    }
    scp->status |= SWITCH_WAIT_REL;
    p = scp->proc;
    PHOLD(p);
    ksignal(p, scp->smode.relsig);
    PRELE(p);
    DPRINTF(5, ("sending relsig to %d\n", scp->pid));
    lwkt_reltoken(&tty_token);

    return TRUE;
}

static int
signal_vt_acq(scr_stat *scp)
{
    struct proc *p;

    lwkt_gettoken(&tty_token);
    if (scp->smode.mode != VT_PROCESS) {
        lwkt_reltoken(&tty_token);
	return FALSE;
    }
    if (scp->sc->unit == sc_console_unit)
	cons_unavail = TRUE;
    scp->status |= SWITCH_WAIT_ACQ;
    p = scp->proc;
    PHOLD(p);
    ksignal(p, scp->smode.acqsig);
    PRELE(p);
    DPRINTF(5, ("sending acqsig to %d\n", scp->pid));
    lwkt_reltoken(&tty_token);

    return TRUE;
}

static int
finish_vt_rel(scr_stat *scp, int release)
{
    lwkt_gettoken(&tty_token);
    if (scp == scp->sc->old_scp && scp->status & SWITCH_WAIT_REL) {
	scp->status &= ~SWITCH_WAIT_REL;
	if (release)
	    do_switch_scr(scp->sc);
	else
	    scp->sc->switch_in_progress = 0;
	lwkt_reltoken(&tty_token);
	return 0;
    }
    lwkt_reltoken(&tty_token);
    return EINVAL;
}

static int
finish_vt_acq(scr_stat *scp)
{
    lwkt_gettoken(&tty_token);
    if (scp == scp->sc->new_scp && scp->status & SWITCH_WAIT_ACQ) {
	scp->status &= ~SWITCH_WAIT_ACQ;
	scp->sc->switch_in_progress = 0;
	lwkt_reltoken(&tty_token);
	return 0;
    }
    lwkt_reltoken(&tty_token);
    return EINVAL;
}

static void
exchange_scr(sc_softc_t *sc)
{
    scr_stat *scp;

    lwkt_gettoken(&tty_token);
    /* save the current state of video and keyboard */
    sc_move_cursor(sc->old_scp, sc->old_scp->xpos, sc->old_scp->ypos);
    if (!ISGRAPHSC(sc->old_scp))
	sc_remove_cursor_image(sc->old_scp);
    if (sc->old_scp->kbd_mode == K_XLATE)
	save_kbd_state(sc->old_scp, TRUE);

    /* set up the video for the new screen */
    scp = sc->cur_scp = sc->new_scp;
    if (sc->old_scp->mode != scp->mode || ISUNKNOWNSC(sc->old_scp))
	set_mode(scp);
    else
	sc_vtb_init(&scp->scr, VTB_FRAMEBUFFER, scp->xsize, scp->ysize,
		    (void *)sc->adp->va_window, FALSE);
    scp->status |= MOUSE_HIDDEN;
    sc_move_cursor(scp, scp->xpos, scp->ypos);
    if (!ISGRAPHSC(scp))
	sc_set_cursor_image(scp);
#ifndef SC_NO_PALETTE_LOADING
    if (ISGRAPHSC(sc->old_scp))
	load_palette(sc->adp, sc->palette);
#endif
    sc_set_border(scp, scp->border);

    /* set up the keyboard for the new screen */
    if (sc->old_scp->kbd_mode != scp->kbd_mode)
	kbd_ioctl(sc->kbd, KDSKBMODE, (caddr_t)&scp->kbd_mode);
    update_kbd_state(scp, scp->status, LOCK_MASK, TRUE);

    mark_all(scp);
    lwkt_reltoken(&tty_token);
}

static void
sc_puts(scr_stat *scp, u_char *buf, int len)
{
#if NSPLASH > 0
    /* make screensaver happy */
    if (!sticky_splash && scp == scp->sc->cur_scp)
	run_scrn_saver = FALSE;
#endif

    if (scp->tsw)
	(*scp->tsw->te_puts)(scp, buf, len);

    if (scp->sc->delayed_next_scr)
	sc_switch_scr(scp->sc, scp->sc->delayed_next_scr - 1);

}

void
sc_draw_cursor_image(scr_stat *scp)
{
    /* assert(scp == scp->sc->cur_scp); */
    ++scp->sc->videoio_in_progress;
    (*scp->rndr->draw_cursor)(scp, scp->cursor_pos,
			      scp->sc->flags & SC_BLINK_CURSOR, TRUE,
			      sc_inside_cutmark(scp, scp->cursor_pos));
    scp->cursor_oldpos = scp->cursor_pos;
    --scp->sc->videoio_in_progress;
}

void
sc_remove_cursor_image(scr_stat *scp)
{
    /* assert(scp == scp->sc->cur_scp); */
    ++scp->sc->videoio_in_progress;
    (*scp->rndr->draw_cursor)(scp, scp->cursor_oldpos,
			      scp->sc->flags & SC_BLINK_CURSOR, FALSE,
			      sc_inside_cutmark(scp, scp->cursor_oldpos));
    --scp->sc->videoio_in_progress;
}

static void
update_cursor_image(scr_stat *scp)
{
    int blink;

    if (scp->sc->flags & SC_CHAR_CURSOR) {
	scp->cursor_base = imax(0, scp->sc->cursor_base);
	scp->cursor_height = imin(scp->sc->cursor_height, scp->font_size);
    } else {
	scp->cursor_base = 0;
	scp->cursor_height = scp->font_size;
    }
    blink = scp->sc->flags & SC_BLINK_CURSOR;

    /* assert(scp == scp->sc->cur_scp); */
    ++scp->sc->videoio_in_progress;
    (*scp->rndr->draw_cursor)(scp, scp->cursor_oldpos, blink, FALSE, 
			      sc_inside_cutmark(scp, scp->cursor_pos));
    (*scp->rndr->set_cursor)(scp, scp->cursor_base, scp->cursor_height, blink);
    (*scp->rndr->draw_cursor)(scp, scp->cursor_pos, blink, TRUE, 
			      sc_inside_cutmark(scp, scp->cursor_pos));
    --scp->sc->videoio_in_progress;
}

void
sc_set_cursor_image(scr_stat *scp)
{
    if (scp->sc->flags & SC_CHAR_CURSOR) {
	scp->cursor_base = imax(0, scp->sc->cursor_base);
	scp->cursor_height = imin(scp->sc->cursor_height, scp->font_size);
    } else {
	scp->cursor_base = 0;
	scp->cursor_height = scp->font_size;
    }

    /* assert(scp == scp->sc->cur_scp); */
    ++scp->sc->videoio_in_progress;
    (*scp->rndr->set_cursor)(scp, scp->cursor_base, scp->cursor_height,
			     scp->sc->flags & SC_BLINK_CURSOR);
    --scp->sc->videoio_in_progress;
}

static void
scinit(int unit, int flags)
{
    /*
     * When syscons is being initialized as the kernel console, malloc()
     * is not yet functional, because various kernel structures has not been
     * fully initialized yet.  Therefore, we need to declare the following
     * static buffers for the console.  This is less than ideal, 
     * but is necessry evil for the time being.  XXX
     */
    static scr_stat main_console;
    static u_short sc_buffer[ROW*COL];	/* XXX */
#ifndef SC_NO_FONT_LOADING
    static u_char font_8[256*8];
    static u_char font_14[256*14];
    static u_char font_16[256*16];
#endif

    sc_softc_t *sc;
    scr_stat *scp;
    video_adapter_t *adp;
    int col;
    int row;
    int i;

    /* one time initialization */
    if (init_done == COLD)
	sc_get_bios_values(&bios_value);
    init_done = WARM;

    /*
     * Allocate resources.  Even if we are being called for the second
     * time, we must allocate them again, because they might have 
     * disappeared...
     */
    sc = sc_get_softc(unit, flags & SC_KERNEL_CONSOLE);
    adp = NULL;
    if (sc->adapter >= 0) {
	vid_release(sc->adp, (void *)&sc->adapter);
	adp = sc->adp;
	sc->adp = NULL;
    }
    if (sc->keyboard >= 0) {
	DPRINTF(5, ("sc%d: releasing kbd%d\n", unit, sc->keyboard));
	i = kbd_release(sc->kbd, (void *)&sc->keyboard);
	DPRINTF(5, ("sc%d: kbd_release returned %d\n", unit, i));
	if (sc->kbd != NULL) {
	    DPRINTF(5, ("sc%d: kbd != NULL!, index:%d, unit:%d, flags:0x%x\n",
		unit, sc->kbd->kb_index, sc->kbd->kb_unit, sc->kbd->kb_flags));
	}
	sc->kbd = NULL;
    }
    sc->adapter = vid_allocate("*", unit, (void *)&sc->adapter);
    sc->adp = vid_get_adapter(sc->adapter);
    /* assert((sc->adapter >= 0) && (sc->adp != NULL)) */
    sc->keyboard = sc_allocate_keyboard(sc, unit);
    DPRINTF(1, ("sc%d: keyboard %d\n", unit, sc->keyboard));
    sc->kbd = kbd_get_keyboard(sc->keyboard);
    if (sc->kbd != NULL) {
	DPRINTF(1, ("sc%d: kbd index:%d, unit:%d, flags:0x%x\n",
		unit, sc->kbd->kb_index, sc->kbd->kb_unit, sc->kbd->kb_flags));
    }

    if (!(sc->flags & SC_INIT_DONE) || (adp != sc->adp)) {

	sc->initial_mode = sc->adp->va_initial_mode;

#ifndef SC_NO_FONT_LOADING
	if (flags & SC_KERNEL_CONSOLE) {
	    sc->font_8 = font_8;
	    sc->font_14 = font_14;
	    sc->font_16 = font_16;
	} else if (sc->font_8 == NULL) {
	    /* assert(sc_malloc) */
	    sc->font_8 = kmalloc(sizeof(font_8), M_SYSCONS, M_WAITOK);
	    sc->font_14 = kmalloc(sizeof(font_14), M_SYSCONS, M_WAITOK);
	    sc->font_16 = kmalloc(sizeof(font_16), M_SYSCONS, M_WAITOK);
	}
#endif

	lwkt_gettoken(&tty_token);
	/* extract the hardware cursor location and hide the cursor for now */
	(*vidsw[sc->adapter]->read_hw_cursor)(sc->adp, &col, &row);
	(*vidsw[sc->adapter]->set_hw_cursor)(sc->adp, -1, -1);
	lwkt_reltoken(&tty_token);

	/* set up the first console */
	sc->first_vty = unit*MAXCONS;
	sc->vtys = MAXCONS;		/* XXX: should be configurable */
	if (flags & SC_KERNEL_CONSOLE) {
	    scp = &main_console;
	    sc->console_scp = scp;
	    init_scp(sc, sc->first_vty, scp);
	    sc_vtb_init(&scp->vtb, VTB_MEMORY, scp->xsize, scp->ysize,
			(void *)sc_buffer, FALSE);
	    if (sc_init_emulator(scp, SC_DFLT_TERM))
		sc_init_emulator(scp, "*");
	    (*scp->tsw->te_default_attr)(scp,
					 kernel_default.std_color,
					 kernel_default.rev_color);
	} else {
	    /* assert(sc_malloc) */
	    sc->dev = kmalloc(sizeof(cdev_t)*sc->vtys, M_SYSCONS, M_WAITOK | M_ZERO);

	    sc->dev[0] = make_dev(&sc_ops, unit*MAXCONS, UID_ROOT, 
				GID_WHEEL, 0600, "ttyv%r", unit*MAXCONS);

	    sc->dev[0]->si_tty = ttymalloc(sc->dev[0]->si_tty);
	    scp = alloc_scp(sc, sc->first_vty);
	    sc->dev[0]->si_drv1 = scp;
	}
	sc->cur_scp = scp;

	/* copy screen to temporary buffer */
	sc_vtb_init(&scp->scr, VTB_FRAMEBUFFER, scp->xsize, scp->ysize,
		    (void *)scp->sc->adp->va_window, FALSE);
	if (ISTEXTSC(scp))
	    sc_vtb_copy(&scp->scr, 0, &scp->vtb, 0, scp->xsize*scp->ysize);

	/* move cursors to the initial positions */
	if (col >= scp->xsize)
	    col = 0;
	if (row >= scp->ysize)
	    row = scp->ysize - 1;
	scp->xpos = col;
	scp->ypos = row;
	scp->cursor_pos = scp->cursor_oldpos = row*scp->xsize + col;
	if (bios_value.cursor_end < scp->font_size)
	    sc->cursor_base = scp->font_size - bios_value.cursor_end - 1;
	else
	    sc->cursor_base = 0;
	i = bios_value.cursor_end - bios_value.cursor_start + 1;
	sc->cursor_height = imin(i, scp->font_size);
#ifndef SC_NO_SYSMOUSE
	sc_mouse_move(scp, scp->xpixel/2, scp->ypixel/2);
#endif
	if (!ISGRAPHSC(scp)) {
    	    sc_set_cursor_image(scp);
    	    sc_draw_cursor_image(scp);
	}

	/* save font and palette */
#ifndef SC_NO_FONT_LOADING
	sc->fonts_loaded = 0;
	if (ISFONTAVAIL(sc->adp->va_flags)) {
#ifdef SC_DFLT_FONT
	    bcopy(dflt_font_8, sc->font_8, sizeof(dflt_font_8));
	    bcopy(dflt_font_14, sc->font_14, sizeof(dflt_font_14));
	    bcopy(dflt_font_16, sc->font_16, sizeof(dflt_font_16));
	    sc->fonts_loaded = FONT_16 | FONT_14 | FONT_8;
	    if (scp->font_size < 14) {
		sc_load_font(scp, 0, 8, sc->font_8, 0, 256);
	    } else if (scp->font_size >= 16) {
		sc_load_font(scp, 0, 16, sc->font_16, 0, 256);
	    } else {
		sc_load_font(scp, 0, 14, sc->font_14, 0, 256);
	    }
#else /* !SC_DFLT_FONT */
	    if (scp->font_size < 14) {
		sc_save_font(scp, 0, 8, sc->font_8, 0, 256);
		sc->fonts_loaded = FONT_8;
	    } else if (scp->font_size >= 16) {
		sc_save_font(scp, 0, 16, sc->font_16, 0, 256);
		sc->fonts_loaded = FONT_16;
	    } else {
		sc_save_font(scp, 0, 14, sc->font_14, 0, 256);
		sc->fonts_loaded = FONT_14;
	    }
#endif /* SC_DFLT_FONT */
	    /* FONT KLUDGE: always use the font page #0. XXX */
	    sc_show_font(scp, 0);
	}
#endif /* !SC_NO_FONT_LOADING */

#ifndef SC_NO_PALETTE_LOADING
	save_palette(sc->adp, sc->palette);
#endif

#if NSPLASH > 0
	if (!(sc->flags & SC_SPLASH_SCRN) && (flags & SC_KERNEL_CONSOLE)) {
	    /* we are ready to put up the splash image! */
	    splash_init(sc->adp, scsplash_callback, sc);
	    sc->flags |= SC_SPLASH_SCRN;
	}
#endif /* NSPLASH */
    }

    /* the rest is not necessary, if we have done it once */
    if (sc->flags & SC_INIT_DONE) {
	return;
    }

    /* initialize mapscrn arrays to a one to one map */
    for (i = 0; i < sizeof(sc->scr_map); i++)
	sc->scr_map[i] = sc->scr_rmap[i] = i;

    sc->flags |= SC_INIT_DONE;
}

static void
scterm(int unit, int flags)
{
    sc_softc_t *sc;
    scr_stat *scp;

    sc = sc_get_softc(unit, flags & SC_KERNEL_CONSOLE);
    if (sc == NULL)
	return;			/* shouldn't happen */

    lwkt_gettoken(&tty_token);
#if NSPLASH > 0
    /* this console is no longer available for the splash screen */
    if (sc->flags & SC_SPLASH_SCRN) {
	splash_term(sc->adp);
	sc->flags &= ~SC_SPLASH_SCRN;
    }
#endif /* NSPLASH */

#if 0 /* XXX */
    /* move the hardware cursor to the upper-left corner */
    (*vidsw[sc->adapter]->set_hw_cursor)(sc->adp, 0, 0);
#endif

    /* release the keyboard and the video card */
    if (sc->keyboard >= 0)
	kbd_release(sc->kbd, &sc->keyboard);
    if (sc->adapter >= 0)
	vid_release(sc->adp, &sc->adapter);

    /* 
     * Stop the terminal emulator, if any.  If operating on the
     * kernel console sc->dev may not be setup yet.
     */
    if (flags & SC_KERNEL_CONSOLE)
	scp = sc->console_scp;
    else
	scp = SC_STAT(sc->dev[0]);
    if (scp->tsw)
	(*scp->tsw->te_term)(scp, &scp->ts);
    if (scp->ts != NULL)
	kfree(scp->ts, M_SYSCONS);

    /* clear the structure */
    if (!(flags & SC_KERNEL_CONSOLE)) {
	/* XXX: We need delete_dev() for this */
	kfree(sc->dev, M_SYSCONS);
#if 0
	/* XXX: We need a ttyunregister for this */
	kfree(sc->tty, M_SYSCONS);
#endif
#ifndef SC_NO_FONT_LOADING
	kfree(sc->font_8, M_SYSCONS);
	kfree(sc->font_14, M_SYSCONS);
	kfree(sc->font_16, M_SYSCONS);
#endif
	/* XXX vtb, history */
    }
    bzero(sc, sizeof(*sc));
    sc->keyboard = -1;
    sc->adapter = -1;
    lwkt_reltoken(&tty_token);
}

static void
scshutdown(void *arg, int howto)
{
    /* assert(sc_console != NULL) */

    lwkt_gettoken(&tty_token);
    syscons_lock();
    sc_touch_scrn_saver();
    if (!cold && sc_console
	&& sc_console->sc->cur_scp->smode.mode == VT_AUTO 
	&& sc_console->smode.mode == VT_AUTO) {
	sc_switch_scr(sc_console->sc, sc_console->index);
    }
    shutdown_in_progress = TRUE;
    syscons_unlock();
    lwkt_reltoken(&tty_token);
}

int
sc_clean_up(scr_stat *scp)
{
#if NSPLASH > 0
    int error;
#endif /* NSPLASH */

    lwkt_gettoken(&tty_token);
    if (scp->sc->flags & SC_SCRN_BLANKED) {
	sc_touch_scrn_saver();
#if NSPLASH > 0
	if ((error = wait_scrn_saver_stop(scp->sc))) {
	    lwkt_reltoken(&tty_token);
	    return error;
	}
#endif /* NSPLASH */
    }
    scp->status |= MOUSE_HIDDEN;
    sc_remove_mouse_image(scp);
    sc_remove_cutmarking(scp);
    lwkt_reltoken(&tty_token);
    return 0;
}

void
sc_alloc_scr_buffer(scr_stat *scp, int wait, int discard)
{
    sc_vtb_t new;
    sc_vtb_t old;

    lwkt_gettoken(&tty_token);
    old = scp->vtb;
    sc_vtb_init(&new, VTB_MEMORY, scp->xsize, scp->ysize, NULL, wait);
    if (!discard && (old.vtb_flags & VTB_VALID)) {
	/* retain the current cursor position and buffer contants */
	scp->cursor_oldpos = scp->cursor_pos;
	/* 
	 * This works only if the old buffer has the same size as or larger 
	 * than the new one. XXX
	 */
	sc_vtb_copy(&old, 0, &new, 0, scp->xsize*scp->ysize);
	scp->vtb = new;
    } else {
	scp->vtb = new;
	sc_vtb_destroy(&old);
    }

#ifndef SC_NO_SYSMOUSE
    /* move the mouse cursor at the center of the screen */
    sc_mouse_move(scp, scp->xpixel / 2, scp->ypixel / 2);
#endif
    lwkt_reltoken(&tty_token);
}

static scr_stat *
alloc_scp(sc_softc_t *sc, int vty)
{
    scr_stat *scp;

    /* assert(sc_malloc) */

    scp = kmalloc(sizeof(scr_stat), M_SYSCONS, M_WAITOK);
    init_scp(sc, vty, scp);

    sc_alloc_scr_buffer(scp, TRUE, TRUE);
    if (sc_init_emulator(scp, SC_DFLT_TERM))
	sc_init_emulator(scp, "*");

#ifndef SC_NO_CUTPASTE
    sc_alloc_cut_buffer(scp, TRUE);
#endif

#ifndef SC_NO_HISTORY
    sc_alloc_history_buffer(scp, 0, 0, TRUE);
#endif
    return scp;
}

/*
 * NOTE: Must be called with tty_token held.
 */
static void
init_scp(sc_softc_t *sc, int vty, scr_stat *scp)
{
    video_info_t info;

    bzero(scp, sizeof(*scp));

    scp->index = vty;
    scp->sc = sc;
    scp->status = 0;
    scp->mode = sc->initial_mode;
    callout_init_mp(&scp->blink_screen_ch);
    lwkt_gettoken(&tty_token);
    (*vidsw[sc->adapter]->get_info)(sc->adp, scp->mode, &info);
    lwkt_reltoken(&tty_token);
    if (info.vi_flags & V_INFO_GRAPHICS) {
	scp->status |= GRAPHICS_MODE;
	scp->xpixel = info.vi_width;
	scp->ypixel = info.vi_height;
	scp->xsize = info.vi_width/8;
	scp->ysize = info.vi_height/info.vi_cheight;
	scp->font_size = 0;
	scp->font = NULL;
    } else {
	scp->xsize = info.vi_width;
	scp->ysize = info.vi_height;
	scp->xpixel = scp->xsize*8;
	scp->ypixel = scp->ysize*info.vi_cheight;
	if (info.vi_cheight < 14) {
	    scp->font_size = 8;
#ifndef SC_NO_FONT_LOADING
	    scp->font = sc->font_8;
#else
	    scp->font = NULL;
#endif
	} else if (info.vi_cheight >= 16) {
	    scp->font_size = 16;
#ifndef SC_NO_FONT_LOADING
	    scp->font = sc->font_16;
#else
	    scp->font = NULL;
#endif
	} else {
	    scp->font_size = 14;
#ifndef SC_NO_FONT_LOADING
	    scp->font = sc->font_14;
#else
	    scp->font = NULL;
#endif
	}
    }
    sc_vtb_init(&scp->vtb, VTB_MEMORY, 0, 0, NULL, FALSE);
    sc_vtb_init(&scp->scr, VTB_FRAMEBUFFER, 0, 0, NULL, FALSE);
    scp->xoff = scp->yoff = 0;
    scp->xpos = scp->ypos = 0;
    scp->start = scp->xsize * scp->ysize - 1;
    scp->end = 0;
    scp->tsw = NULL;
    scp->ts = NULL;
    scp->rndr = NULL;
    scp->border = BG_BLACK;
    scp->cursor_base = sc->cursor_base;
    scp->cursor_height = imin(sc->cursor_height, scp->font_size);
    scp->mouse_cut_start = scp->xsize*scp->ysize;
    scp->mouse_cut_end = -1;
    scp->mouse_signal = 0;
    scp->mouse_pid = 0;
    scp->mouse_proc = NULL;
    scp->kbd_mode = K_XLATE;
    scp->bell_pitch = bios_value.bell_pitch;
    scp->bell_duration = BELL_DURATION;
    scp->status |= (bios_value.shift_state & NLKED);
    scp->status |= CURSOR_ENABLED | MOUSE_HIDDEN;
    scp->pid = 0;
    scp->proc = NULL;
    scp->smode.mode = VT_AUTO;
    scp->history = NULL;
    scp->history_pos = 0;
    scp->history_size = 0;
}

int
sc_init_emulator(scr_stat *scp, char *name)
{
    sc_term_sw_t *sw;
    sc_rndr_sw_t *rndr;
    void *p;
    int error;

    if (name == NULL)	/* if no name is given, use the current emulator */
	sw = scp->tsw;
    else		/* ...otherwise find the named emulator */
	sw = sc_term_match(name);
    if (sw == NULL) {
	return EINVAL;
    }

    rndr = NULL;
    if (strcmp(sw->te_renderer, "*") != 0) {
	rndr = sc_render_match(scp, sw->te_renderer, scp->model);
    }
    if (rndr == NULL) {
	rndr = sc_render_match(scp, scp->sc->adp->va_name, scp->model);
	if (rndr == NULL) {
	    return ENODEV;
	}
    }

    if (sw == scp->tsw) {
	error = (*sw->te_init)(scp, &scp->ts, SC_TE_WARM_INIT);
	scp->rndr = rndr;
	sc_clear_screen(scp);
	/* assert(error == 0); */
	return error;
    }

    if (sc_malloc && (sw->te_size > 0))
	p = kmalloc(sw->te_size, M_SYSCONS, M_NOWAIT);
    else
	p = NULL;
    error = (*sw->te_init)(scp, &p, SC_TE_COLD_INIT);
    if (error) {
	return error;
    }

    if (scp->tsw)
	(*scp->tsw->te_term)(scp, &scp->ts);
    if (scp->ts != NULL)
	kfree(scp->ts, M_SYSCONS);
    scp->tsw = sw;
    scp->ts = p;
    scp->rndr = rndr;

    /* XXX */
    (*sw->te_default_attr)(scp, user_default.std_color, user_default.rev_color);
    sc_clear_screen(scp);

    return 0;
}

/*
 * scgetc(flags) - get character from keyboard.
 * If flags & SCGETC_CN, then avoid harmful side effects.
 * If flags & SCGETC_NONBLOCK, then wait until a key is pressed, else
 * return NOKEY if there is nothing there.
 */
static u_int
scgetc(sc_softc_t *sc, u_int flags)
{
    scr_stat *scp;
#ifndef SC_NO_HISTORY
    struct tty *tp;
#endif
    u_int c;
    int this_scr;
    int f;
    int i;

    lwkt_gettoken(&tty_token);
    if (sc->kbd == NULL) {
        lwkt_reltoken(&tty_token);
	return NOKEY;
    }

next_code:
#if 1
    /* I don't like this, but... XXX */
    if (flags & SCGETC_CN) {
	syscons_lock();
	sccnupdate(sc->cur_scp);
	syscons_unlock();
    }
#endif
    scp = sc->cur_scp;
    /* first see if there is something in the keyboard port */
    for (;;) {
	c = kbd_read_char(sc->kbd, !(flags & SCGETC_NONBLOCK));
	if (c == ERRKEY) {
	    if (!(flags & SCGETC_CN))
		sc_bell(scp, bios_value.bell_pitch, BELL_DURATION);
	} else if (c == NOKEY) {
	    lwkt_reltoken(&tty_token);
	    return c;
	} else {
	    break;
	}
    }

    /* make screensaver happy */
    if (!(c & RELKEY))
	sc_touch_scrn_saver();

    if (!(flags & SCGETC_CN))
	/* do the /dev/random device a favour */
	add_keyboard_randomness(c);

    if (scp->kbd_mode != K_XLATE) {
        lwkt_reltoken(&tty_token);
	return KEYCHAR(c);
    }

    /* if scroll-lock pressed allow history browsing */
    if (!ISGRAPHSC(scp) && scp->history && scp->status & SLKED) {

	scp->status &= ~CURSOR_ENABLED;
	sc_remove_cursor_image(scp);

#ifndef SC_NO_HISTORY
	if (!(scp->status & BUFFER_SAVED)) {
	    scp->status |= BUFFER_SAVED;
	    sc_hist_save(scp);
	}
	switch (c) {
	/* FIXME: key codes */
	case SPCLKEY | FKEY | F(49):  /* home key */
	    sc_remove_cutmarking(scp);
	    sc_hist_home(scp);
	    goto next_code;

	case SPCLKEY | FKEY | F(57):  /* end key */
	    sc_remove_cutmarking(scp);
	    sc_hist_end(scp);
	    goto next_code;

	case SPCLKEY | FKEY | F(50):  /* up arrow key */
	    sc_remove_cutmarking(scp);
	    if (sc_hist_up_line(scp))
		if (!(flags & SCGETC_CN))
		    sc_bell(scp, bios_value.bell_pitch, BELL_DURATION);
	    goto next_code;

	case SPCLKEY | FKEY | F(58):  /* down arrow key */
	    sc_remove_cutmarking(scp);
	    if (sc_hist_down_line(scp))
		if (!(flags & SCGETC_CN))
		    sc_bell(scp, bios_value.bell_pitch, BELL_DURATION);
	    goto next_code;

	case SPCLKEY | FKEY | F(51):  /* page up key */
	    sc_remove_cutmarking(scp);
	    for (i=0; i<scp->ysize; i++) {
		if (sc_hist_up_line(scp)) {
		    if (!(flags & SCGETC_CN))
			sc_bell(scp, bios_value.bell_pitch, BELL_DURATION);
		    break;
		}
	    }
	    goto next_code;

	case SPCLKEY | FKEY | F(59):  /* page down key */
	    sc_remove_cutmarking(scp);
	    for (i=0; i<scp->ysize; i++) {
		if (sc_hist_down_line(scp)) {
		    if (!(flags & SCGETC_CN))
			sc_bell(scp, bios_value.bell_pitch, BELL_DURATION);
		    break;
		}
	    }
	    goto next_code;
	}
#endif /* SC_NO_HISTORY */
    }

    /* 
     * Process and consume special keys here.  Return a plain char code
     * or a char code with the META flag or a function key code.
     */
    if (c & RELKEY) {
	/* key released */
	/* goto next_code */
    } else {
	/* key pressed */
	if (c & SPCLKEY) {
	    c &= ~SPCLKEY;
	    switch (KEYCHAR(c)) {
	    /* LOCKING KEYS */
	    case NLK: case CLK: case ALK:
		break;
	    case SLK:
		kbd_ioctl(sc->kbd, KDGKBSTATE, (caddr_t)&f);
		if (f & SLKED) {
		    scp->status |= SLKED;
		} else {
		    if (scp->status & SLKED) {
			scp->status &= ~SLKED;
#ifndef SC_NO_HISTORY
			if (scp->status & BUFFER_SAVED) {
			    if (!sc_hist_restore(scp))
				sc_remove_cutmarking(scp);
			    scp->status &= ~BUFFER_SAVED;
			    scp->status |= CURSOR_ENABLED;
			    sc_draw_cursor_image(scp);
			}
			tp = VIRTUAL_TTY(sc, scp->index);
			if (ISTTYOPEN(tp))
			    scstart(tp);
#endif
		    }
		}
		break;

	    /* NON-LOCKING KEYS */
	    case NOP:
	    case LSH:  case RSH:  case LCTR: case RCTR:
	    case LALT: case RALT: case ASH:  case META:
		break;

	    case BTAB:
		if (!(sc->flags & SC_SCRN_BLANKED)) {
                    lwkt_reltoken(&tty_token);
		    return c;
		}
		break;

	    case SPSC:
#if NSPLASH > 0
		/* force activatation/deactivation of the screen saver */
		if (!(sc->flags & SC_SCRN_BLANKED)) {
		    run_scrn_saver = TRUE;
		    sc->scrn_time_stamp -= scrn_blank_time;
		}
		if (cold) {
		    /*
		     * While devices are being probed, the screen saver need
		     * to be invoked explictly. XXX
		     */
		    if (sc->flags & SC_SCRN_BLANKED) {
			scsplash_stick(FALSE);
			stop_scrn_saver(sc, current_saver);
		    } else {
			if (!ISGRAPHSC(scp)) {
			    scsplash_stick(TRUE);
			    (*current_saver)(sc, TRUE);
			}
		    }
		}
#endif /* NSPLASH */
		break;

	    case RBT:
#ifndef SC_DISABLE_REBOOT
		shutdown_nice(0);
#endif
		break;

	    case HALT:
#ifndef SC_DISABLE_REBOOT
		shutdown_nice(RB_HALT);
#endif
		break;

	    case PDWN:
#ifndef SC_DISABLE_REBOOT
		shutdown_nice(RB_HALT|RB_POWEROFF);
#endif
		break;

#if __i386__ && NAPM > 0
	    case SUSP:
		apm_suspend(PMST_SUSPEND);
		break;
	    case STBY:
		apm_suspend(PMST_STANDBY);
		break;
#else
	    case SUSP:
	    case STBY:
		break;
#endif

	    case DBG:
#ifndef SC_DISABLE_DDBKEY
#ifdef DDB
		lwkt_reltoken(&tty_token);
		Debugger("manual escape to debugger");
		lwkt_gettoken(&tty_token);
#else
		kprintf("No debugger in kernel\n");
#endif
#else /* SC_DISABLE_DDBKEY */
		/* do nothing */
#endif /* SC_DISABLE_DDBKEY */
		break;

	    case PNC:
		if (enable_panic_key)
			panic("Forced by the panic key");
		break;

	    case NEXT:
		this_scr = scp->index;
		for (i = (this_scr - sc->first_vty + 1)%sc->vtys;
			sc->first_vty + i != this_scr; 
			i = (i + 1)%sc->vtys) {
		    struct tty *tp = VIRTUAL_TTY(sc, sc->first_vty + i);
		    if (ISTTYOPEN(tp)) {
			syscons_lock();
			sc_switch_scr(scp->sc, sc->first_vty + i);
			syscons_unlock();
			break;
		    }
		}
		break;

	    case PREV:
		this_scr = scp->index;
		for (i = (this_scr - sc->first_vty + sc->vtys - 1)%sc->vtys;
			sc->first_vty + i != this_scr;
			i = (i + sc->vtys - 1)%sc->vtys) {
		    struct tty *tp = VIRTUAL_TTY(sc, sc->first_vty + i);
		    if (ISTTYOPEN(tp)) {
			syscons_lock();
			sc_switch_scr(scp->sc, sc->first_vty + i);
			syscons_unlock();
			break;
		    }
		}
		break;

	    default:
		if (KEYCHAR(c) >= F_SCR && KEYCHAR(c) <= L_SCR) {
		    syscons_lock();
		    sc_switch_scr(scp->sc, sc->first_vty + KEYCHAR(c) - F_SCR);
		    syscons_unlock();
		    break;
		}
		/* assert(c & FKEY) */
		if (!(sc->flags & SC_SCRN_BLANKED)) {
		    lwkt_reltoken(&tty_token);
		    return c;
		}
		break;
	    }
	    /* goto next_code */
	} else {
	    /* regular keys (maybe MKEY is set) */
	    if (!(sc->flags & SC_SCRN_BLANKED)) {
		lwkt_reltoken(&tty_token);
		return c;
	    }
	}
    }

    goto next_code;
}

int
scmmap(struct dev_mmap_args *ap)
{
    scr_stat *scp;

    lwkt_gettoken(&tty_token);
    scp = SC_STAT(ap->a_head.a_dev);
    if (scp != scp->sc->cur_scp) {
        lwkt_reltoken(&tty_token);
	return EINVAL;
    }
    ap->a_result = (*vidsw[scp->sc->adapter]->mmap)(scp->sc->adp, ap->a_offset,
						    ap->a_nprot);
    lwkt_reltoken(&tty_token);
    return(0);
}

static int
save_kbd_state(scr_stat *scp, int unlock)
{
    int state;
    int error;

    WANT_UNLOCK(unlock);
    error = kbd_ioctl(scp->sc->kbd, KDGKBSTATE, (caddr_t)&state);
    WANT_LOCK(unlock);

    if (error == ENOIOCTL)
	error = ENODEV;
    if (error == 0) {
	scp->status &= ~LOCK_MASK;
	scp->status |= state;
    }
    return error;
}

static int
update_kbd_state(scr_stat *scp, int new_bits, int mask, int unlock)
{
    int state;
    int error;

    if (mask != LOCK_MASK) {
	WANT_UNLOCK(unlock);
	error = kbd_ioctl(scp->sc->kbd, KDGKBSTATE, (caddr_t)&state);
	WANT_LOCK(unlock);

	if (error == ENOIOCTL)
	    error = ENODEV;
	if (error) {
	    return error;
	}
	state &= ~mask;
	state |= new_bits & mask;
    } else {
	state = new_bits & LOCK_MASK;
    }
    WANT_UNLOCK(unlock);
    error = kbd_ioctl(scp->sc->kbd, KDSKBSTATE, (caddr_t)&state);
    WANT_LOCK(unlock);
    if (error == ENOIOCTL)
	error = ENODEV;
    return error;
}

static int
update_kbd_leds(scr_stat *scp, int which)
{
    int error;

    which &= LOCK_MASK;
    error = kbd_ioctl(scp->sc->kbd, KDSETLED, (caddr_t)&which);
    if (error == ENOIOCTL)
	error = ENODEV;
    return error;
}

int
set_mode(scr_stat *scp)
{
    video_info_t info;

    lwkt_gettoken(&tty_token);
    /* reject unsupported mode */
    if ((*vidsw[scp->sc->adapter]->get_info)(scp->sc->adp, scp->mode, &info)) {
        lwkt_reltoken(&tty_token);
	return 1;
    }

    /* if this vty is not currently showing, do nothing */
    if (scp != scp->sc->cur_scp) {
        lwkt_reltoken(&tty_token);
	return 0;
    }

    /* setup video hardware for the given mode */
    (*vidsw[scp->sc->adapter]->set_mode)(scp->sc->adp, scp->mode);
    sc_vtb_init(&scp->scr, VTB_FRAMEBUFFER, scp->xsize, scp->ysize,
		(void *)scp->sc->adp->va_window, FALSE);

#ifndef SC_NO_FONT_LOADING
    /* load appropriate font */
    if (!(scp->status & GRAPHICS_MODE)) {
	if (!(scp->status & PIXEL_MODE) && ISFONTAVAIL(scp->sc->adp->va_flags)) {
	    if (scp->font_size < 14) {
		if (scp->sc->fonts_loaded & FONT_8)
		    sc_load_font(scp, 0, 8, scp->sc->font_8, 0, 256);
	    } else if (scp->font_size >= 16) {
		if (scp->sc->fonts_loaded & FONT_16)
		    sc_load_font(scp, 0, 16, scp->sc->font_16, 0, 256);
	    } else {
		if (scp->sc->fonts_loaded & FONT_14)
		    sc_load_font(scp, 0, 14, scp->sc->font_14, 0, 256);
	    }
	    /*
	     * FONT KLUDGE:
	     * This is an interim kludge to display correct font.
	     * Always use the font page #0 on the video plane 2.
	     * Somehow we cannot show the font in other font pages on
	     * some video cards... XXX
	     */ 
	    sc_show_font(scp, 0);
	}
	mark_all(scp);
    }
#endif /* !SC_NO_FONT_LOADING */

    sc_set_border(scp, scp->border);
    sc_set_cursor_image(scp);

    lwkt_reltoken(&tty_token);
    return 0;
}

void
refresh_ega_palette(scr_stat *scp)
{
    uint32_t r, g, b;
    int reg;
    int rsize, gsize, bsize;
    int rfld, gfld, bfld;
    int i;

    rsize = scp->sc->adp->va_info.vi_pixel_fsizes[0];
    gsize = scp->sc->adp->va_info.vi_pixel_fsizes[1];
    bsize = scp->sc->adp->va_info.vi_pixel_fsizes[2];
    rfld = scp->sc->adp->va_info.vi_pixel_fields[0];
    gfld = scp->sc->adp->va_info.vi_pixel_fields[1];
    bfld = scp->sc->adp->va_info.vi_pixel_fields[2];

    for (i = 0; i < 16; i++) {
	reg = scp->sc->adp->va_palette_regs[i];

	r = scp->sc->palette[reg * 3] >> (8 - rsize);
	g = scp->sc->palette[reg * 3 + 1] >> (8 - gsize);
	b = scp->sc->palette[reg * 3 + 2] >> (8 - bsize);

	scp->ega_palette[i] = (r << rfld) + (g << gfld) + (b << bfld);
    }
}

void
sc_set_border(scr_stat *scp, int color)
{
    ++scp->sc->videoio_in_progress;
    (*scp->rndr->draw_border)(scp, color);
    --scp->sc->videoio_in_progress;
}

#ifndef SC_NO_FONT_LOADING
void
sc_load_font(scr_stat *scp, int page, int size, u_char *buf,
	     int base, int count)
{
    sc_softc_t *sc;

    sc = scp->sc;
    sc->font_loading_in_progress = TRUE;
    (*vidsw[sc->adapter]->load_font)(sc->adp, page, size, buf, base, count);
    sc->font_loading_in_progress = FALSE;
}

void
sc_save_font(scr_stat *scp, int page, int size, u_char *buf,
	     int base, int count)
{
    sc_softc_t *sc;

    sc = scp->sc;
    sc->font_loading_in_progress = TRUE;
    (*vidsw[sc->adapter]->save_font)(sc->adp, page, size, buf, base, count);
    sc->font_loading_in_progress = FALSE;
}

void
sc_show_font(scr_stat *scp, int page)
{
    (*vidsw[scp->sc->adapter]->show_font)(scp->sc->adp, page);
}
#endif /* !SC_NO_FONT_LOADING */

void
sc_paste(scr_stat *scp, u_char *p, int count) 
{
    struct tty *tp;
    u_char *rmap;

    lwkt_gettoken(&tty_token);
    if (scp->status & MOUSE_VISIBLE) {
	tp = VIRTUAL_TTY(scp->sc, scp->sc->cur_scp->index);
	if (!ISTTYOPEN(tp)) {
	    lwkt_reltoken(&tty_token);
	    return;
	}
	rmap = scp->sc->scr_rmap;
	for (; count > 0; --count)
	    (*linesw[tp->t_line].l_rint)(rmap[*p++], tp);
    }
    lwkt_reltoken(&tty_token);
}

void
sc_bell(scr_stat *scp, int pitch, int duration)
{
    if (cold || shutdown_in_progress)
	return;

    if (scp != scp->sc->cur_scp && (scp->sc->flags & SC_QUIET_BELL)) {
	return;
    }

    if (scp->sc->flags & SC_VISUAL_BELL) {
	if (scp->sc->blink_in_progress) {
	    return;
	}
	scp->sc->blink_in_progress = 3;
	if (scp != scp->sc->cur_scp)
	    scp->sc->blink_in_progress += 2;
	sc_blink_screen(scp->sc->cur_scp);
    } else if (duration != 0 && pitch != 0) {
	if (scp != scp->sc->cur_scp)
	    pitch *= 2;
	sysbeep(pitch, duration);
    }
}

/*
 * Two versions of blink_screen(), one called from the console path
 * with the syscons locked, and one called from a timer callout.
 */
static void
sc_blink_screen(scr_stat *scp)
{
    if (ISGRAPHSC(scp) || (scp->sc->blink_in_progress <= 1)) {
	scp->sc->blink_in_progress = 0;
	mark_all(scp);
	if (scp->sc->delayed_next_scr)
	    sc_switch_scr(scp->sc, scp->sc->delayed_next_scr - 1);
    } else {
	(*scp->rndr->draw)(scp, 0, scp->xsize*scp->ysize,
			   scp->sc->blink_in_progress & 1);
	scp->sc->blink_in_progress--;
    }
}

#if 0
static void
blink_screen_callout(void *arg)
{
    scr_stat *scp = arg;
    struct tty *tp;

    if (ISGRAPHSC(scp) || (scp->sc->blink_in_progress <= 1)) {
	syscons_lock();
	scp->sc->blink_in_progress = 0;
    	mark_all(scp);
	syscons_unlock();
	tp = VIRTUAL_TTY(scp->sc, scp->index);
	if (ISTTYOPEN(tp))
	    scstart(tp);
	if (scp->sc->delayed_next_scr) {
	    syscons_lock();
	    sc_switch_scr(scp->sc, scp->sc->delayed_next_scr - 1);
	    syscons_unlock();
	}
    } else {
	syscons_lock();
	(*scp->rndr->draw)(scp, 0, scp->xsize*scp->ysize, 
			   scp->sc->blink_in_progress & 1);
	scp->sc->blink_in_progress--;
	syscons_unlock();
	callout_reset(&scp->blink_screen_ch, hz / 10,
		      blink_screen_callout, scp);
    }
}
#endif

/*
 * Allocate active keyboard. Try to allocate "kbdmux" keyboard first, and,
 * if found, add all non-busy keyboards to "kbdmux". Otherwise look for
 * any keyboard.
 */

static int
sc_allocate_keyboard(sc_softc_t *sc, int unit)
{
	int		 idx0, idx;
	keyboard_t	*k0, *k;
	keyboard_info_t	 ki;

	idx0 = kbd_allocate("kbdmux", -1, (void *)&sc->keyboard, sckbdevent, sc);
	if (idx0 != -1) {
		k0 = kbd_get_keyboard(idx0);

		for (idx = kbd_find_keyboard2("*", -1, 0, 0);
		     idx != -1;
		     idx = kbd_find_keyboard2("*", -1, idx + 1, 0)) {
			k = kbd_get_keyboard(idx);

			if (idx == idx0 || KBD_IS_BUSY(k))
				continue;

			bzero(&ki, sizeof(ki));
			strcpy(ki.kb_name, k->kb_name);
			ki.kb_unit = k->kb_unit;

			kbd_ioctl(k0, KBADDKBD, (caddr_t) &ki);
		}
	} else
		idx0 = kbd_allocate("*", unit, (void *)&sc->keyboard, sckbdevent, sc);

	return (idx0);
}
