/*-
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: head/sys/dev/led/led.c 247008 2013-02-19 19:25:50Z mav $
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/sbuf.h>
#include <sys/queue.h>
#include <dev/misc/led/led.h>
#include <sys/uio.h>
#include <sys/device.h>

struct ledsc {
	LIST_ENTRY(ledsc)	list;
	char			*name;
	void			*private;
	int			unit;
	led_t			*func;
	struct cdev *dev;
	struct sbuf		*spec;
	char			*str;
	char			*ptr;
	int			count;
	time_t			last_second;
};

static struct unrhdr *led_unit;
static struct lock led_lock;
static struct lock led_lock2;
static LIST_HEAD(, ledsc) led_list = LIST_HEAD_INITIALIZER(led_list);
static struct callout led_ch;
static int blinkers = 0;

static MALLOC_DEFINE(M_LED, "LED", "LED driver");

static void
led_timeout(void *p)
{
	struct ledsc	*sc;

	lockmgr(&led_lock, LK_EXCLUSIVE);
	LIST_FOREACH(sc, &led_list, list) {
		if (sc->ptr == NULL)
			continue;
		if (sc->count > 0) {
			sc->count--;
			continue;
		}
		if (*sc->ptr == '.') {
			sc->ptr = NULL;
			blinkers--;
			continue;
		} else if (*sc->ptr == 'U' || *sc->ptr == 'u') {
			if (sc->last_second == time_second)
				continue;
			sc->last_second = time_second;
			sc->func(sc->private, *sc->ptr == 'U');
		} else if (*sc->ptr >= 'a' && *sc->ptr <= 'j') {
			sc->func(sc->private, 0);
			sc->count = (*sc->ptr & 0xf) - 1;
		} else if (*sc->ptr >= 'A' && *sc->ptr <= 'J') {
			sc->func(sc->private, 1);
			sc->count = (*sc->ptr & 0xf) - 1;
		}
		sc->ptr++;
		if (*sc->ptr == '\0')
			sc->ptr = sc->str;
	}
	if (blinkers > 0)
		callout_reset(&led_ch, hz / 10, led_timeout, p);
	lockmgr(&led_lock, LK_RELEASE);
}

static int
led_state(struct ledsc *sc, struct sbuf **sb, int state)
{
	struct sbuf *sb2 = NULL;

	sb2 = sc->spec;
	sc->spec = *sb;
	if (*sb != NULL) {
		sc->str = sbuf_data(*sb);
		if (sc->ptr == NULL) {
			blinkers++;
			callout_reset(&led_ch, hz / 10, led_timeout, NULL);
		}
		sc->ptr = sc->str;
	} else {
		sc->str = NULL;
		if (sc->ptr != NULL)
			blinkers--;
		sc->ptr = NULL;
		sc->func(sc->private, state);
	}
	sc->count = 0;
	*sb = sb2;
	return(0);
}

static int
led_parse(const char *s, struct sbuf **sb, int *state)
{
	int i, error;

	/*
	 * Handle "on" and "off" immediately so people can flash really
	 * fast from userland if they want to
	 */
	if (*s == '0' || *s == '1') {
		*state = *s & 1;
		return (0);
	}

	*state = 0;
	*sb = sbuf_new_auto();
	if (*sb == NULL)
		return (ENOMEM);
	switch(s[0]) {
		/*
		 * Flash, default is 100msec/100msec.
		 * 'f2' sets 200msec/200msec etc.
		 */
		case 'f':
			if (s[1] >= '1' && s[1] <= '9')
				i = s[1] - '1';
			else
				i = 0;
			sbuf_printf(*sb, "%c%c", 'A' + i, 'a' + i);
			break;
		/*
		 * Digits, flashes out numbers.
		 * 'd12' becomes -__________-_-______________________________
		 */
		case 'd':
			for(s++; *s; s++) {
				if (!isdigit(*s))
					continue;
				i = *s - '0';
				if (i == 0)
					i = 10;
				for (; i > 1; i--) 
					sbuf_cat(*sb, "Aa");
				sbuf_cat(*sb, "Aj");
			}
			sbuf_cat(*sb, "jj");
			break;
		/*
		 * String, roll your own.
		 * 'a-j' gives "off" for n/10 sec.
		 * 'A-J' gives "on" for n/10 sec.
		 * no delay before repeat
		 * 'sAaAbBa' becomes _-_--__-
		 */
		case 's':
			for(s++; *s; s++) {
				if ((*s >= 'a' && *s <= 'j') ||
				    (*s >= 'A' && *s <= 'J') ||
				    *s == 'U' || *s <= 'u' ||
					*s == '.')
					sbuf_bcat(*sb, s, 1);
			}
			break;
		/*
		 * Morse.
		 * '.' becomes _-
		 * '-' becomes _---
		 * ' ' becomes __
		 * '\n' becomes ____
		 * 1sec pause between repeats
		 * '... --- ...' -> _-_-_-___---_---_---___-_-_-__________
		 */
		case 'm':
			for(s++; *s; s++) {
				if (*s == '.')
					sbuf_cat(*sb, "aA");
				else if (*s == '-')
					sbuf_cat(*sb, "aC");
				else if (*s == ' ')
					sbuf_cat(*sb, "b");
				else if (*s == '\n')
					sbuf_cat(*sb, "d");
			}
			sbuf_cat(*sb, "j");
			break;
		default:
			sbuf_delete(*sb);
			return (EINVAL);
	}
	error = sbuf_finish(*sb);
	if (error != 0 || sbuf_len(*sb) == 0) {
		*sb = NULL;
		return (error);
	}
	return (0);
}

static int
led_open(struct dev_open_args *ap)
{
	return (0);
}

static int
led_close(struct dev_close_args *ap)
{
	return (0);
}

static int
led_write(struct dev_write_args *ap)
{
	struct uio *uio = ap->a_uio;
	cdev_t dev = ap->a_head.a_dev;
	struct ledsc	*sc;
	char *s;
	struct sbuf *sb = NULL;
	int error, state = 0;

	if (uio->uio_resid > 512)
		return (EINVAL);
	s = kmalloc(uio->uio_resid + 1, M_DEVBUF, M_WAITOK);
	s[uio->uio_resid] = '\0';
	error = uiomove(s, uio->uio_resid, uio);
	if (error) {
		kfree(s, M_DEVBUF);
		return (error);
	}
	error = led_parse(s, &sb, &state);
	kfree(s, M_DEVBUF);
	if (error)
		return (error);
	lockmgr(&led_lock, LK_EXCLUSIVE);
	sc = dev->si_drv1;
	if (sc != NULL)
		error = led_state(sc, &sb, state);
	lockmgr(&led_lock, LK_RELEASE);
	if (sb != NULL)
		sbuf_delete(sb);
	return (error);
}

int
led_set(char const *name, char const *cmd)
{
	struct ledsc	*sc;
	struct sbuf *sb = NULL;
	int error, state = 0;

	error = led_parse(cmd, &sb, &state);
	if (error)
		return (error);
	lockmgr(&led_lock, LK_EXCLUSIVE);
	LIST_FOREACH(sc, &led_list, list) {
		if (strcmp(sc->name, name) == 0)
			break;
	}
	if (sc != NULL)
		error = led_state(sc, &sb, state);
	else
		error = ENOENT;
	lockmgr(&led_lock, LK_RELEASE);
	if (sb != NULL)
		sbuf_delete(sb);
	return (0);
}

static struct dev_ops led_ops = {
	{ "LED", 0, 0 },
	.d_open =	led_open,
	.d_close =	led_close,
	.d_write =	led_write,
};

struct cdev *
led_create(led_t *func, void *priv, char const *name)
{

	return (led_create_state(func, priv, name, 0));
}
struct cdev *
led_create_state(led_t *func, void *priv, char const *name, int state)
{
	struct ledsc	*sc;

	sc = kmalloc(sizeof *sc, M_LED, M_WAITOK | M_ZERO);

	lockmgr(&led_lock2, LK_EXCLUSIVE);
	sc->name = kstrdup(name, M_LED);
	sc->unit = alloc_unr(led_unit);
	sc->private = priv;
	sc->func = func;
	sc->dev = make_dev(&led_ops, sc->unit,
	    UID_ROOT, GID_WHEEL, 0600, "led/%s", name);
	lockmgr(&led_lock2, LK_RELEASE);

	lockmgr(&led_lock, LK_EXCLUSIVE);
	sc->dev->si_drv1 = sc;
	LIST_INSERT_HEAD(&led_list, sc, list);
	sc->func(sc->private, state != 0);
	lockmgr(&led_lock, LK_RELEASE);

	return (sc->dev);
}

void
led_destroy(struct cdev *dev)
{
	struct ledsc *sc;

	lockmgr(&led_lock, LK_EXCLUSIVE);
	sc = dev->si_drv1;
	dev->si_drv1 = NULL;
	if (sc->ptr != NULL)
		blinkers--;
	LIST_REMOVE(sc, list);
	if (LIST_EMPTY(&led_list))
		callout_stop(&led_ch);
	lockmgr(&led_lock, LK_RELEASE);

	lockmgr(&led_lock2, LK_EXCLUSIVE);
	free_unr(led_unit, sc->unit);
	destroy_dev(dev);
	if (sc->spec != NULL)
		sbuf_delete(sc->spec);
	kfree(sc->name, M_LED);
	kfree(sc, M_LED);
	lockmgr(&led_lock2, LK_RELEASE);
}

static void
led_drvinit(void *unused)
{

	led_unit = new_unrhdr(0, INT_MAX, NULL);
	lockinit(&led_lock, "LED lock", 0, LK_CANRECURSE);
	lockinit(&led_lock2, "LED lock2", 0, LK_CANRECURSE);
	callout_init_mp(&led_ch);
}

SYSINIT(leddev, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, led_drvinit, NULL);
