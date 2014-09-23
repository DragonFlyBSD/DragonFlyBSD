/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $Id: if_mn.c,v 1.1 1999/02/01 13:06:40 phk Exp $
 *
 * Driver for Siemens reference design card "Easy321-R1".
 *
 * This card contains a FALC54 E1/T1 framer and a MUNICH32X 32-channel HDLC
 * controller. 
 *
 * The driver supports E1 mode with up to 31 channels.  We send CRC4 but don't
 * check it coming in.
 *
 * The FALC54 and MUNICH32X have far too many registers and weird modes for
 * comfort, so I have not bothered typing it all into a "fooreg.h" file,
 * you will (badly!) need the documentation anyway if you want to mess with
 * this gadget.
 *
 * $FreeBSD: src/sys/pci/if_mn.c,v 1.11.2.3 2001/01/23 12:47:09 phk Exp $
 */

/*
 * Stuff to describe the MUNIC32X and FALC54 chips.
 */

#define M32_CHAN	32	/* We have 32 channels */
#define M32_TS		32	/* We have 32 timeslots */

#define NG_MN_NODE_TYPE	"mn"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#include "pci_if.h"

#include <machine/clock.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>  


static int mn_maxlatency = 1000;
SYSCTL_INT(_debug, OID_AUTO, mn_maxlatency, CTLFLAG_RW, 
    &mn_maxlatency, 0, 
	"The number of milliseconds a packet is allowed to spend in the output queue.  "
	"If the output queue is longer than this number of milliseconds when the packet "
	"arrives for output, the packet will be dropped."
);

#ifndef NMN
/* Most machines don't support more than 4 busmaster PCI slots, if even that many */
#define NMN	4
#endif

/* From: PEB 20321 data sheet, p187, table 22 */
struct m32xreg {
	u_int32_t conf,    cmd,     stat,    imask;
	u_int32_t fill10,  piqba,   piql,    fill1c;
	u_int32_t mode1,   mode2,   ccba,    txpoll;
	u_int32_t tiqba,   tiql,    riqba,   riql;
	u_int32_t lconf,   lccba,   fill48,  ltran;
	u_int32_t ltiqba,  ltiql,   lriqba,  lriql;
	u_int32_t lreg0,   lreg1,   lreg2,   lreg3;
	u_int32_t lreg4,   lreg5,   lre6,    lstat;
	u_int32_t gpdir,   gpdata,  gpod,    fill8c;
	u_int32_t ssccon,  sscbr,   ssctb,   sscrb;
	u_int32_t ssccse,  sscim,   fillab,  fillac;
	u_int32_t iomcon1, iomcon2, iomstat, fillbc; 
	u_int32_t iomcit0, iomcit1, iomcir0, iomcir1;
	u_int32_t iomtmo,  iomrmo,  filld8,  filldc;
	u_int32_t mbcmd,   mbdata1, mbdata2, mbdata3;
	u_int32_t mbdata4, mbdata5, mbdata6, mbdata7;
};

/* From: PEB 2254 data sheet, p80, table 10 */
struct f54wreg {
	u_int16_t xfifo;
	u_int8_t                  cmdr,   mode,   rah1,   rah2,   ral1,   ral2;
	u_int8_t  ipc,    ccr1,   ccr3,   pre,    rtr1,   rtr2,   rtr3,   rtr4;
	u_int8_t  ttr1,   ttr2,   ttr3,   ttr4,   imr0,   imr1,   imr2,   imr3;
	u_int8_t  imr4,   fill19, fmr0,   fmr1,   fmr2,   loop,   xsw,    xsp;
	u_int8_t  xc0,    xc1,    rc0,    rc1,    xpm0,   xpm1,   xpm2,   tswm;
	u_int8_t  test1,  idle,   xsa4,   xsa5,   xsa6,   xsa7,   xsa8,   fmr3;
	u_int8_t  icb1,   icb2,   icb3,   icb4,   lim0,   lim1,   pcd,    pcr;
	u_int8_t  lim2,   fill39[7];
	u_int8_t  fill40[8];
	u_int8_t  fill48[8];
	u_int8_t  fill50[8];
	u_int8_t  fill58[8];
	u_int8_t  dec,    fill61, test2,  fill63[5];
	u_int8_t  fill68[8];
	u_int8_t  xs[16];
};

/* From: PEB 2254 data sheet, p117, table 10 */
struct f54rreg {
	u_int16_t rfifo;
	u_int8_t                  fill2,  mode,   rah1,   rah2,   ral1,   ral2;
	u_int8_t  ipc,    ccr1,   ccr3,   pre,    rtr1,   rtr2,   rtr3,   rtr4;
	u_int8_t  ttr1,   ttr2,   ttr3,   ttr4,   imr0,   imr1,   imr2,   imr3;
	u_int8_t  imr4,   fill19, fmr0,   fmr1,   fmr2,   loop,   xsw,    xsp;
	u_int8_t  xc0,    xc1,    rc0,    rc1,    xpm0,   xpm1,   xpm2,   tswm;
	u_int8_t  test,   idle,   xsa4,   xsa5,   xsa6,   xsa7,   xsa8,   fmr13;
	u_int8_t  icb1,   icb2,   icb3,   icb4,   lim0,   lim1,   pcd,    pcr;
	u_int8_t  lim2,   fill39[7];
	u_int8_t  fill40[8];
	u_int8_t  fill48[4],                      frs0,   frs1,   rsw,    rsp;
	u_int16_t fec,            cvc,            cec1,           ebc;
	u_int16_t cec2,           cec3;
	u_int8_t                                  rsa4,   rsa5,   rsa6,   rsa7;
	u_int8_t  rsa8,   rsa6s,  tsr0,   tsr1,   sis,    rsis;
	u_int16_t                                                 rbc;
	u_int8_t  isr0,   isr1,   isr2,   isr3,   fill6c, fill6d, gis,    vstr;
	u_int8_t  rs[16];
};

/* Transmit & receive descriptors */
struct trxd {
	u_int32_t	flags;
	vm_offset_t	next;
	vm_offset_t	data;
	u_int32_t	status;	/* only used for receive */
	struct mbuf	*m;	/* software use only */
	struct trxd	*vnext;	/* software use only */
};

/* Channel specification */
struct cspec {
	u_int32_t	flags;
	vm_offset_t	rdesc;
	vm_offset_t	tdesc;
	u_int32_t	itbs;
};

struct m32_mem {
	vm_offset_t	csa;
	u_int32_t	ccb;
	u_int32_t	reserve1[2];
	u_int32_t	ts[M32_TS];
	struct cspec	cs[M32_CHAN];
	vm_offset_t	crxd[M32_CHAN];
	vm_offset_t	ctxd[M32_CHAN];
};

struct softc;
struct sockaddr;
struct rtentry;

static	int	mn_probe  (device_t self);
static	int	mn_attach (device_t self);
static	void	mn_create_channel(struct softc *sc, int chan);
static	int	mn_reset(struct softc *sc);
static	struct trxd * mn_alloc_desc(void);
static	void	mn_free_desc(struct trxd *dp);
static	void	mn_intr(void *xsc);
static	u_int32_t mn_parse_ts(const char *s, int *nbit);
#ifdef notyet
static	void	m32_dump(struct softc *sc);
static	void	f54_dump(struct softc *sc);
static	void	mn_fmt_ts(char *p, u_int32_t ts);
#endif /* notyet */
static	void	f54_init(struct softc *sc);

static	ng_constructor_t ngmn_constructor;
static	ng_rcvmsg_t ngmn_rcvmsg;
static	ng_shutdown_t ngmn_shutdown;
static	ng_newhook_t ngmn_newhook;
static	ng_connect_t ngmn_connect;
static	ng_rcvdata_t ngmn_rcvdata;
static	ng_disconnect_t ngmn_disconnect;

static struct ng_type mntypestruct = {
	NG_VERSION,
	NG_MN_NODE_TYPE,
	NULL, 
	ngmn_constructor,
	ngmn_rcvmsg,
	ngmn_shutdown,
	ngmn_newhook,
	NULL,
	ngmn_connect,
	ngmn_rcvdata,
	ngmn_rcvdata,
	ngmn_disconnect,
	NULL
};

static MALLOC_DEFINE(M_MN, "mn", "Mx driver related");

#define NIQB	64

struct schan {
	enum {DOWN, UP} state;
	struct softc	*sc;
	int		chan;
	u_int32_t	ts;
	char		name[8];
	struct trxd	*r1, *rl;
	struct trxd	*x1, *xl;
	hook_p		hook;

	time_t		last_recv;
	time_t		last_rxerr;
	time_t		last_xmit;

	u_long		rx_error;

	u_long		short_error;
	u_long		crc_error;
	u_long		dribble_error;
	u_long		long_error;
	u_long		abort_error;
	u_long		overflow_error;

	int		last_error;
	int		prev_error;

	u_long		tx_pending;
	u_long		tx_limit;
};

enum framing {WHOKNOWS, E1, E1U, T1, T1U};

struct softc {
	int	unit;
	device_t	dev;
	struct resource *irq;
	void *intrhand;
	enum framing	framing;
	int 		nhooks;
	void 		*m0v, *m1v;
	vm_offset_t	m0p, m1p;
	struct m32xreg	*m32x;
	struct f54wreg	*f54w;
	struct f54rreg	*f54r;
	struct m32_mem	m32_mem;
	u_int32_t	tiqb[NIQB];
	u_int32_t	riqb[NIQB];
	u_int32_t	piqb[NIQB];
	u_int32_t	ltiqb[NIQB];
	u_int32_t	lriqb[NIQB];
	char		name[8];
	u_int32_t	falc_irq, falc_state, framer_state;
	struct schan *ch[M32_CHAN];
	char	nodename[NG_NODESIZ];
	node_p	node;

	u_long		cnt_fec;
	u_long		cnt_cvc;
	u_long		cnt_cec1;
	u_long		cnt_ebc;
	u_long		cnt_cec2;
	u_long		cnt_cec3;
	u_long		cnt_rbc;
};

static int
ngmn_constructor(node_p *nodep)
{

	return (EINVAL);
}

static int
ngmn_shutdown(node_p nodep)
{

	return (EINVAL);
}

static void
ngmn_config(node_p node, char *set, char *ret)
{
	struct softc *sc;
	enum framing wframing;

	sc = node->private;

	if (set != NULL) {
		if (!strncmp(set, "line ", 5)) {
			wframing = sc->framing;
			if (!strcmp(set, "line e1")) {
				wframing = E1;
			} else if (!strcmp(set, "line e1u")) {
				wframing = E1U;
			} else {
				strcat(ret, "ENOGROK\n");
				return;
			}
			if (wframing == sc->framing)
				return;
			if (sc->nhooks > 0) {
				ksprintf(ret, "Cannot change line when %d hooks open\n", sc->nhooks);
				return;
			}
			sc->framing = wframing;
#if 1
			f54_init(sc);
#else
			mn_reset(sc);
#endif
		} else {
			kprintf("%s CONFIG SET [%s]\n", sc->nodename, set);
			strcat(ret, "ENOGROK\n");
			return;
		}
	}
	
}

static int
ngmn_rcvmsg(node_p node, struct ng_mesg *msg, const char *retaddr, struct ng_mesg **resp)
{
	struct softc *sc;
	struct schan *sch;
	char *s, *r;
	int pos, i;

	sc = node->private;

	if (msg->header.typecookie != NGM_GENERIC_COOKIE) {
		if (resp != NULL)
			*resp = NULL;
		kfree(msg, M_NETGRAPH);
		return (EINVAL);
	}
		
	if (msg->header.cmd != NGM_TEXT_CONFIG &&
	    msg->header.cmd != NGM_TEXT_STATUS) {
		if (resp != NULL)
			*resp = NULL;
		kfree(msg, M_NETGRAPH);
		return (EINVAL);
	}

	NG_MKRESPONSE(*resp, msg, sizeof(struct ng_mesg) + NG_TEXTRESPONSE,
	    M_INTWAIT);
	if (*resp == NULL) {
		kfree(msg, M_NETGRAPH);
		return (ENOMEM);
	}

	if (msg->header.arglen) 
		s = (char *)msg->data;
	else
		s = NULL;
	r = (char *)(*resp)->data;
	*r = '\0';

	if (msg->header.cmd == NGM_TEXT_CONFIG) {
		ngmn_config(node, s, r);
		(*resp)->header.arglen = strlen(r) + 1;
		kfree(msg, M_NETGRAPH);
		return (0);
	}

	pos = 0;
	pos += ksprintf(pos + r,"Framer status %b;\n", sc->framer_state, "\20"
	    "\40LOS\37AIS\36LFA\35RRA"
	    "\34AUXP\33NMF\32LMFA\31frs0.0"
	    "\30frs1.7\27TS16RA\26TS16LOS\25TS16AIS"
	    "\24TS16LFA\23frs1.2\22XLS\21XLO"
	    "\20RS1\17rsw.6\16RRA\15RY0"
	    "\14RY1\13RY2\12RY3\11RY4"
	    "\10SI1\7SI2\6rsp.5\5rsp.4"
	    "\4rsp.3\3RSIF\2RS13\1RS15");
	pos += ksprintf(pos + r,"    Framing errors: %lu", sc->cnt_fec);
	pos += ksprintf(pos + r,"  Code Violations: %lu\n", sc->cnt_cvc);
	
	pos += ksprintf(pos + r,"    Falc State %b;\n", sc->falc_state, "\20"
	    "\40LOS\37AIS\36LFA\35RRA"
	    "\34AUXP\33NMF\32LMFA\31frs0.0"
	    "\30frs1.7\27TS16RA\26TS16LOS\25TS16AIS"
	    "\24TS16LFA\23frs1.2\22XLS\21XLO"
	    "\20RS1\17rsw.6\16RRA\15RY0"
	    "\14RY1\13RY2\12RY3\11RY4"
	    "\10SI1\7SI2\6rsp.5\5rsp.4"
	    "\4rsp.3\3RSIF\2RS13\1RS15");
	pos += ksprintf(pos + r, "    Falc IRQ %b\n", sc->falc_irq, "\20"
	    "\40RME\37RFS\36T8MS\35RMB\34CASC\33CRC4\32SA6SC\31RPF"
	    "\30b27\27RDO\26ALLS\25XDU\24XMB\23b22\22XLSC\21XPR"
	    "\20FAR\17LFA\16MFAR\15T400MS\14AIS\13LOS\12RAR\11RA"
	    "\10ES\7SEC\6LMFA16\5AIS16\4RA16\3API\2SLN\1SLP");
	for (i = 0; i < M32_CHAN; i++) {
		if (!sc->ch[i])
			continue;
		sch = sc->ch[i];

		pos += ksprintf(r + pos, "  Chan %d <%s> ",
		    i, sch->hook->name);

		pos += ksprintf(r + pos, "  Last Rx: ");
		if (sch->last_recv)
			pos += ksprintf(r + pos, "%lu s", time_uptime - sch->last_recv);
		else
			pos += ksprintf(r + pos, "never");

		pos += ksprintf(r + pos, ", last RxErr: ");
		if (sch->last_rxerr)
			pos += ksprintf(r + pos, "%lu s", time_uptime - sch->last_rxerr);
		else
			pos += ksprintf(r + pos, "never");

		pos += ksprintf(r + pos, ", last Tx: ");
		if (sch->last_xmit)
			pos += ksprintf(r + pos, "%lu s\n", time_uptime - sch->last_xmit);
		else
			pos += ksprintf(r + pos, "never\n");

		pos += ksprintf(r + pos, "    RX error(s) %lu", sch->rx_error);
		pos += ksprintf(r + pos, " Short: %lu", sch->short_error);
		pos += ksprintf(r + pos, " CRC: %lu", sch->crc_error);
		pos += ksprintf(r + pos, " Mod8: %lu", sch->dribble_error);
		pos += ksprintf(r + pos, " Long: %lu", sch->long_error);
		pos += ksprintf(r + pos, " Abort: %lu", sch->abort_error);
		pos += ksprintf(r + pos, " Overflow: %lu\n", sch->overflow_error);

		pos += ksprintf(r + pos, "    Last error: %b  Prev error: %b\n",
		    sch->last_error, "\20\7SHORT\5CRC\4MOD8\3LONG\2ABORT\1OVERRUN",
		    sch->prev_error, "\20\7SHORT\5CRC\4MOD8\3LONG\2ABORT\1OVERRUN");
		pos += ksprintf(r + pos, "    Xmit bytes pending %ld\n",
		    sch->tx_pending);
	}
	(*resp)->header.arglen = pos + 1;
	kfree(msg, M_NETGRAPH);
	return (0);
}

static int
ngmn_newhook(node_p node, hook_p hook, const char *name)
{
	u_int32_t ts, chan;
	struct softc *sc;
	int nbit;

	sc = node->private;

	if (name[0] != 't' || name[1] != 's')
		return (EINVAL);

	ts = mn_parse_ts(name + 2, &nbit);
	kprintf("%d bits %x\n", nbit, ts);
	if (sc->framing == E1 && (ts & 1))
		return (EINVAL);
	if (sc->framing == E1U && nbit != 32)
		return (EINVAL);
	if (ts == 0)
		return (EINVAL);
	if (sc->framing == E1)
		chan = ffs(ts) - 1;
	else
		chan = 1;
	if (!sc->ch[chan])
		mn_create_channel(sc, chan);
	else if (sc->ch[chan]->state == UP)
		return (EBUSY);
	sc->ch[chan]->ts = ts;
	sc->ch[chan]->hook = hook;
	sc->ch[chan]->tx_limit = nbit * 8;
	hook->private = sc->ch[chan];
	sc->nhooks++;
	return(0);
}


static struct trxd *mn_desc_free;

static struct trxd *
mn_alloc_desc(void)
{
	struct trxd *dp;

	dp = mn_desc_free;
	if (dp) 
		mn_desc_free = dp->vnext;
	else
		dp = (struct trxd *)kmalloc(sizeof *dp, M_MN, M_INTWAIT);
	return (dp);
}

static void
mn_free_desc(struct trxd *dp)
{
	dp->vnext =  mn_desc_free;
	mn_desc_free = dp;
}

static u_int32_t
mn_parse_ts(const char *s, int *nbit)
{
	unsigned r;
	int i, j;
	char *p;

	r = 0;
	j = -1;
	*nbit = 0;
	while(*s) {
		i = strtol(s, &p, 0);
		if (i < 0 || i > 31)
			return (0);
		while (j != -1 && j < i) {
			r |= 1 << j++;
			(*nbit)++;
		}
		j = -1;
		r |= 1 << i;
		(*nbit)++;
		if (*p == ',') {
			s = p + 1;
			continue;
		} else if (*p == '-') {
			j = i + 1;
			s = p + 1;
			continue;
		} else if (!*p) {
			break;
		} else {
			return (0);
		}
	}
	return (r);
}

#ifdef notyet
static void
mn_fmt_ts(char *p, u_int32_t ts)
{
	char *s;
	int j;

	s = "";
	ts &= 0xffffffff;
	for (j = 0; j < 32; j++) {
		if (!(ts & (1 << j)))
			continue;
		ksprintf(p, "%s%d", s, j);
		p += strlen(p);
		s = ",";
		if (!(ts & (1 << (j+1)))) 
			continue;
		for (; j < 32; j++)
			if (!(ts & (1 << (j+1))))
				break;
		ksprintf(p, "-%d", j);
		p += strlen(p);
		s = ",";
	}
}
#endif /* notyet */

/*
 * OUTPUT
 */

static int
ngmn_rcvdata(hook_p hook, struct mbuf *m, meta_p meta)
{
	struct mbuf  *m2;
	struct trxd *dp, *dp2;
	struct schan *sch;
	struct softc *sc;
	int chan, pitch, len;

	sch = hook->private;
	sc = sch->sc;
	chan = sch->chan;

	if (sch->state != UP) {
		NG_FREE_DATA(m, meta);
		return (0);
	}
	if (sch->tx_pending + m->m_pkthdr.len > sch->tx_limit * mn_maxlatency) {
		NG_FREE_DATA(m, meta);
		return (0);
	}
	NG_FREE_META(meta);
	pitch = 0;
	m2 = m;
	dp2 = sc->ch[chan]->xl;
	len = m->m_pkthdr.len;
	while (len) {
		dp = mn_alloc_desc();
		if (!dp) {
			pitch++;
			m_freem(m);
			sc->ch[chan]->xl = dp2;
			dp = dp2->vnext;
			while (dp) {
				dp2 = dp->vnext;
				mn_free_desc(dp);
				dp = dp2;
			}
			sc->ch[chan]->xl->vnext = NULL;
			break;
		}
		dp->data = vtophys(m2->m_data);
		dp->flags = m2->m_len << 16;
		dp->flags += 1;
		len -= m2->m_len;
		dp->next = vtophys(dp);
		dp->vnext = NULL;
		sc->ch[chan]->xl->next = vtophys(dp);
		sc->ch[chan]->xl->vnext = dp;
		sc->ch[chan]->xl = dp;
		if (!len) {
			dp->m = m;
			dp->flags |= 0xc0000000;
			dp2->flags &= ~0x40000000;
		} else {
			dp->m = NULL;
			m2 = m2->m_next;
		}
	} 
	if (pitch)
		kprintf("%s%d: Short on mem, pitched %d packets\n", 
		    sc->name, chan, pitch);
	else {
#if 0
		kprintf("%d = %d + %d (%p)\n",
		    sch->tx_pending + m->m_pkthdr.len,
		    sch->tx_pending , m->m_pkthdr.len, m);
#endif
		sch->tx_pending += m->m_pkthdr.len;
		sc->m32x->txpoll &= ~(1 << chan);
	}
	return (0);
}

/*
 * OPEN
 */
static int
ngmn_connect(hook_p hook)
{
	int i, nts, chan;
	struct trxd *dp, *dp2;
	struct mbuf *m;
	struct softc *sc;
	struct schan *sch;
	u_int32_t u;

	sch = hook->private;
	chan = sch->chan;
	sc = sch->sc;

	if (sch->state == UP) 
		return (0);
	sch->state = UP;

	/* Count and configure the timeslots for this channel */
	for (nts = i = 0; i < 32; i++)
		if (sch->ts & (1 << i)) {
			sc->m32_mem.ts[i] = 0x00ff00ff |
				(chan << 24) | (chan << 8);
			nts++;
		}

	/* Init the receiver & xmitter to HDLC */
	sc->m32_mem.cs[chan].flags = 0x80e90006;
	/* Allocate two buffers per timeslot */
	if (nts == 32)
		sc->m32_mem.cs[chan].itbs = 63;
	else
		sc->m32_mem.cs[chan].itbs = nts * 2;

	/* Setup a transmit chain with one descriptor */
	/* XXX: we actually send a 1 byte packet */
	dp = mn_alloc_desc();
	MGETHDR(m, MB_WAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	m->m_pkthdr.len = 0;
	dp->m = m;
	dp->flags = 0xc0000000 + (1 << 16);
	dp->next = vtophys(dp);
	dp->vnext = NULL;
	dp->data = vtophys(sc->name);
	sc->m32_mem.cs[chan].tdesc = vtophys(dp);
	sc->ch[chan]->x1 = dp;
	sc->ch[chan]->xl = dp;

	/* Setup a receive chain with 5 + NTS descriptors */

	dp = mn_alloc_desc();
	m = NULL;
	MGETHDR(m, MB_WAIT, MT_DATA);
	if (m == NULL) {
		mn_free_desc(dp);
		return (ENOBUFS);
	}
	MCLGET(m, MB_WAIT);
	if ((m->m_flags & M_EXT) == 0) {
		mn_free_desc(dp);
		m_freem(m);
		return (ENOBUFS);
	}
	dp->m = m;
	dp->data = vtophys(m->m_data);
	dp->flags = 0x40000000;
	dp->flags += 1600 << 16;
	dp->next = vtophys(dp);
	dp->vnext = NULL;
	sc->ch[chan]->rl = dp;

	for (i = 0; i < (nts + 10); i++) {
		dp2 = dp;
		dp = mn_alloc_desc();
		m = NULL;
		MGETHDR(m, MB_WAIT, MT_DATA);
		if (m == NULL) {
			mn_free_desc(dp);
			m_freem(m);
			return (ENOBUFS);
		}
		MCLGET(m, MB_WAIT);
		if ((m->m_flags & M_EXT) == 0) {
			mn_free_desc(dp);
			m_freem(m);
			return (ENOBUFS);
		}
		dp->m = m;
		dp->data = vtophys(m->m_data);
		dp->flags = 0x00000000;
		dp->flags += 1600 << 16;
		dp->next = vtophys(dp2);
		dp->vnext = dp2;
	}
	sc->m32_mem.cs[chan].rdesc = vtophys(dp);
	sc->ch[chan]->r1 = dp;

	/* Initialize this channel */
	sc->m32_mem.ccb = 0x00008000 + (chan << 8);
	sc->m32x->cmd = 0x1;
	DELAY(1000);
	u = sc->m32x->stat; 
	if (!(u & 1))
		kprintf("%s: init chan %d stat %08x\n", sc->name, chan, u);
	sc->m32x->stat = 1; 

	return (0);
}

/*
 * CLOSE
 */
static int
ngmn_disconnect(hook_p hook)
{
	int chan, i;
	struct softc *sc;
	struct schan *sch;
	struct trxd *dp, *dp2;
	u_int32_t u;

	sch = hook->private;
	chan = sch->chan;
	sc = sch->sc;
	
	if (sch->state == DOWN) 
		return (0);
	sch->state = DOWN;

	/* Set receiver & transmitter off */
	sc->m32_mem.cs[chan].flags = 0x80920006;
	sc->m32_mem.cs[chan].itbs = 0;

	/* free the timeslots */
	for (i = 0; i < 32; i++)
		if (sc->ch[chan]->ts & (1 << i)) 
			sc->m32_mem.ts[i] = 0x20002000;

	/* Initialize this channel */
	sc->m32_mem.ccb = 0x00008000 + (chan << 8);
	sc->m32x->cmd = 0x1;
	DELAY(30);
	u = sc->m32x->stat; 
	if (!(u & 1))
		kprintf("%s: zap chan %d stat %08x\n", sc->name, chan, u);
	sc->m32x->stat = 1; 
	
	/* Free all receive descriptors and mbufs */
	for (dp = sc->ch[chan]->r1; dp ; dp = dp2) {
		if (dp->m)
			m_freem(dp->m);
		sc->ch[chan]->r1 = dp2 = dp->vnext;
		mn_free_desc(dp);
	}

	/* Free all transmit descriptors and mbufs */
	for (dp = sc->ch[chan]->x1; dp ; dp = dp2) {
		if (dp->m) {
			sc->ch[chan]->tx_pending -= dp->m->m_pkthdr.len;
			m_freem(dp->m);
		}
		sc->ch[chan]->x1 = dp2 = dp->vnext;
		mn_free_desc(dp);
	}
	sc->nhooks--;
	return(0);
}

/*
 * Create a new channel.
 */
static void
mn_create_channel(struct softc *sc, int chan)
{
	struct schan *sch;

	sch = sc->ch[chan] = (struct schan *)kmalloc(sizeof *sc->ch[chan], 
	    M_MN, M_WAITOK | M_ZERO);
	sch->sc = sc;
	sch->state = DOWN;
	sch->chan = chan;
	ksprintf(sch->name, "%s%d", sc->name, chan);
	return;
}

#ifdef notyet
/*
 * Dump Munich32x state
 */
static void
m32_dump(struct softc *sc)
{
	u_int32_t *tp4;
	int i, j;

	kprintf("mn%d: MUNICH32X dump\n", sc->unit);
	tp4 = (u_int32_t *)sc->m0v;
	for(j = 0; j < 64; j += 8) {
		kprintf("%02x", j * sizeof *tp4);
		for(i = 0; i < 8; i++)
			kprintf(" %08x", tp4[i+j]);
		kprintf("\n");
	}
	for(j = 0; j < M32_CHAN; j++) {
		if (!sc->ch[j])
			continue;
		kprintf("CH%d: state %d ts %08x", 
			j, sc->ch[j]->state, sc->ch[j]->ts);
		kprintf("  %08x %08x %08x %08x %08x %08x\n",
			sc->m32_mem.cs[j].flags,
			sc->m32_mem.cs[j].rdesc,
			sc->m32_mem.cs[j].tdesc,
			sc->m32_mem.cs[j].itbs,
			sc->m32_mem.crxd[j],
			sc->m32_mem.ctxd[j] );
	}
}

/*
 * Dump Falch54 state
 */
static void
f54_dump(struct softc *sc)
{
	u_int8_t *tp1;
	int i, j;

	kprintf("%s: FALC54 dump\n", sc->name);
	tp1 = (u_int8_t *)sc->m1v;
	for(j = 0; j < 128; j += 16) {
		kprintf("%s: %02x |", sc->name, j * sizeof *tp1);
		for(i = 0; i < 16; i++)
			kprintf(" %02x", tp1[i+j]);
		kprintf("\n");
	}
}
#endif /* notyet */

/*
 * Init Munich32x
 */
static void
m32_init(struct softc *sc)
{

	sc->m32x->conf =  0x00000000;
	sc->m32x->mode1 = 0x81048000 + 1600; 	/* XXX: temp */
#if 1
	sc->m32x->mode2 = 0x00000081;
	sc->m32x->txpoll = 0xffffffff;
#elif 1
	sc->m32x->mode2 = 0x00000081;
	sc->m32x->txpoll = 0xffffffff;
#else
	sc->m32x->mode2 = 0x00000101;
#endif
	sc->m32x->lconf = 0x6060009B;
	sc->m32x->imask = 0x00000000;
}

/*
 * Init the Falc54
 */
static void
f54_init(struct softc *sc)
{
	sc->f54w->ipc  = 0x07;

	sc->f54w->xpm0 = 0xbd;
	sc->f54w->xpm1 = 0x03;
	sc->f54w->xpm2 = 0x00;

	sc->f54w->imr0 = 0x18; /* RMB, CASC */
	sc->f54w->imr1 = 0x08; /* XMB */
	sc->f54w->imr2 = 0x00; 
	sc->f54w->imr3 = 0x38; /* LMFA16, AIS16, RA16 */
	sc->f54w->imr4 = 0x00; 

	sc->f54w->fmr0 = 0xf0; /* X: HDB3, R: HDB3 */
	sc->f54w->fmr1 = 0x0e; /* Send CRC4, 2Mbit, ECM */
	if (sc->framing == E1)
		sc->f54w->fmr2 = 0x03; /* Auto Rem-Alarm, Auto resync */
	else if (sc->framing == E1U)
		sc->f54w->fmr2 = 0x33; /* dais, rtm, Auto Rem-Alarm, Auto resync */

	sc->f54w->lim1 = 0xb0; /* XCLK=8kHz, .62V threshold */
	sc->f54w->pcd =  0x0a;
	sc->f54w->pcr =  0x15;
	sc->f54w->xsw =  0x9f; /* fmr4 */
	if (sc->framing == E1)
		sc->f54w->xsp =  0x1c; /* fmr5 */
	else if (sc->framing == E1U)
		sc->f54w->xsp =  0x3c; /* tt0, fmr5 */
	sc->f54w->xc0 =  0x07;
	sc->f54w->xc1 =  0x3d;
	sc->f54w->rc0 =  0x05;
	sc->f54w->rc1 =  0x00;
	sc->f54w->cmdr = 0x51;
}

static int
mn_reset(struct softc *sc)
{
	u_int32_t u;
	int i;

	sc->m32x->ccba = vtophys(&sc->m32_mem.csa);
	sc->m32_mem.csa = vtophys(&sc->m32_mem.ccb);

	bzero(sc->tiqb, sizeof sc->tiqb);
	sc->m32x->tiqba = vtophys(&sc->tiqb);
	sc->m32x->tiql = NIQB / 16 - 1;

	bzero(sc->riqb, sizeof sc->riqb);
	sc->m32x->riqba = vtophys(&sc->riqb);
	sc->m32x->riql = NIQB / 16 - 1;

	bzero(sc->ltiqb, sizeof sc->ltiqb);
	sc->m32x->ltiqba = vtophys(&sc->ltiqb);
	sc->m32x->ltiql = NIQB / 16 - 1;

	bzero(sc->lriqb, sizeof sc->lriqb);
	sc->m32x->lriqba = vtophys(&sc->lriqb);
	sc->m32x->lriql = NIQB / 16 - 1;

	bzero(sc->piqb, sizeof sc->piqb);
	sc->m32x->piqba = vtophys(&sc->piqb);
	sc->m32x->piql = NIQB / 16 - 1;

	m32_init(sc);
	f54_init(sc);

	u = sc->m32x->stat; 
	sc->m32x->stat = u;
	sc->m32_mem.ccb = 0x4;
	sc->m32x->cmd = 0x1;
	DELAY(1000);
	u = sc->m32x->stat;
	sc->m32x->stat = u;

	/* set all timeslots to known state */
	for (i = 0; i < 32; i++)
		sc->m32_mem.ts[i] = 0x20002000;

	if (!(u & 1)) {
		kprintf(
"mn%d: WARNING: Controller failed the PCI bus-master test.\n"
"mn%d: WARNING: Use a PCI slot which can support bus-master cards.\n",
		    sc->unit, sc->unit);
		return  (0);
	}
	return (1);
}

/*
 * FALC54 interrupt handling
 */
static void
f54_intr(struct softc *sc)
{
	unsigned u, s;
#if 0
	unsigned g;
	g = sc->f54r->gis;
#endif
	u = sc->f54r->isr0 << 24;
	u |= sc->f54r->isr1 << 16;
	u |= sc->f54r->isr2 <<  8;
	u |= sc->f54r->isr3;
	sc->falc_irq = u;
	/* don't chat about the 1 sec heart beat */
	if (u & ~0x40) {
#if 0
		kprintf("%s*: FALC54 IRQ GIS:%02x %b\n", sc->name, g, u, "\20"
		    "\40RME\37RFS\36T8MS\35RMB\34CASC\33CRC4\32SA6SC\31RPF"
		    "\30b27\27RDO\26ALLS\25XDU\24XMB\23b22\22XLSC\21XPR"
		    "\20FAR\17LFA\16MFAR\15T400MS\14AIS\13LOS\12RAR\11RA"
		    "\10ES\7SEC\6LMFA16\5AIS16\4RA16\3API\2SLN\1SLP");
#endif
		s = sc->f54r->frs0 << 24;
		s |= sc->f54r->frs1 << 16;
		s |= sc->f54r->rsw <<  8;
		s |= sc->f54r->rsp;
		sc->falc_state = s;

		s &= ~0x01844038;	/* undefined or static bits */
		s &= ~0x00009fc7;	/* bits we don't care about */
		s &= ~0x00780000;	/* XXX: TS16 related */
		s &= ~0x06000000;	/* XXX: Multiframe related */
#if 0
		kprintf("%s*: FALC54 Status %b\n", sc->name, s, "\20"
		    "\40LOS\37AIS\36LFA\35RRA\34AUXP\33NMF\32LMFA\31frs0.0"
		    "\30frs1.7\27TS16RA\26TS16LOS\25TS16AIS\24TS16LFA\23frs1.2\22XLS\21XLO"
		    "\20RS1\17rsw.6\16RRA\15RY0\14RY1\13RY2\12RY3\11RY4"
		    "\10SI1\7SI2\6rsp.5\5rsp.4\4rsp.3\3RSIF\2RS13\1RS15");
#endif
		if (s != sc->framer_state) {
#if 0
			for (i = 0; i < M32_CHAN; i++) {
				if (!sc->ch[i])
					continue;
			        sp = &sc->ch[i]->ifsppp;
				if (!(sp->pp_if.if_flags & IFF_UP))
					continue;
				if (s) 
					timeout((timeout_t *)sp->pp_down, sp, 1 * hz);
				else 
					timeout((timeout_t *)sp->pp_up, sp, 1 * hz);
			}
#endif
			sc->framer_state = s;
		}
	} 
	/* Once per second check error counters */
	/* XXX: not clear if this is actually ok */
	if (!(u & 0x40))
		return;
	sc->cnt_fec  += sc->f54r->fec;
	sc->cnt_cvc  += sc->f54r->cvc;
	sc->cnt_cec1 += sc->f54r->cec1;
	sc->cnt_ebc  += sc->f54r->ebc;
	sc->cnt_cec2 += sc->f54r->cec2;
	sc->cnt_cec3 += sc->f54r->cec3;
	sc->cnt_rbc  += sc->f54r->rbc;
}

/*
 * Transmit interrupt for one channel
 */
static void
mn_tx_intr(struct softc *sc, u_int32_t vector)
{
	u_int32_t chan;
	struct trxd *dp;
	struct mbuf *m;

	chan = vector & 0x1f;
	if (!sc->ch[chan]) 
		return;
	if (sc->ch[chan]->state != UP) {
		kprintf("%s: tx_intr when not UP\n", sc->name);
		return;
	}
	for (;;) {
		dp = sc->ch[chan]->x1;
		if (vtophys(dp) == sc->m32_mem.ctxd[chan]) 
			return;
		m = dp->m;
		if (m) {
#if 0
			kprintf("%d = %d - %d (%p)\n",
			    sc->ch[chan]->tx_pending - m->m_pkthdr.len,
			    sc->ch[chan]->tx_pending , m->m_pkthdr.len, m);
#endif
			sc->ch[chan]->tx_pending -= m->m_pkthdr.len;
			m_freem(m);
		}
		sc->ch[chan]->last_xmit = time_uptime;
		sc->ch[chan]->x1 = dp->vnext;
		mn_free_desc(dp);
	}
}

/*
 * Receive interrupt for one channel
 */
static void
mn_rx_intr(struct softc *sc, u_int32_t vector)
{
	u_int32_t chan, err;
	struct trxd *dp;
	struct mbuf *m;
	struct schan *sch;

	chan = vector & 0x1f;
	if (!sc->ch[chan])
		return;
	sch = sc->ch[chan];
	if (sch->state != UP) {
		kprintf("%s: rx_intr when not UP\n", sc->name);
		return;
	}
	vector &= ~0x1f;
	if (vector == 0x30000b00)
		sch->rx_error++;
	for (;;) {
		dp = sch->r1;
		if (vtophys(dp) == sc->m32_mem.crxd[chan]) 
			return;
		m = dp->m;
		dp->m = NULL;
		m->m_pkthdr.len = m->m_len = (dp->status >> 16) & 0x1fff;
		err = (dp->status >> 8) & 0xff;
		if (!err) {
			ng_queue_data(sch->hook, m, NULL);
			sch->last_recv = time_uptime;
			m = NULL;
			/* we could be down by now... */
			if (sch->state != UP) 
				return;
		} else if (err & 0x40) {
			sch->short_error++;
		} else if (err & 0x10) {
			sch->crc_error++;
		} else if (err & 0x08) {
			sch->dribble_error++;
		} else if (err & 0x04) {
			sch->long_error++;
		} else if (err & 0x02) {
			sch->abort_error++;
		} else if (err & 0x01) {
			sch->overflow_error++;
		}
		if (err) {
			sch->last_rxerr = time_uptime;
			sch->prev_error = sch->last_error;
			sch->last_error = err;
		}

		sc->ch[chan]->r1 = dp->vnext;

		/* Replenish desc + mbuf supplies */
		if (!m) {
			MGETHDR(m, MB_DONTWAIT, MT_DATA);
			if (m == NULL) {
				mn_free_desc(dp);
				return; /* ENOBUFS */
			}
			MCLGET(m, MB_DONTWAIT);
			if((m->m_flags & M_EXT) == 0) {
				mn_free_desc(dp);
				m_freem(m);
				return; /* ENOBUFS */
			}
		}
		dp->m = m;
		dp->data = vtophys(m->m_data);
		dp->flags = 0x40000000;
		dp->flags += 1600 << 16;
		dp->next = vtophys(dp);
		dp->vnext = NULL;
		sc->ch[chan]->rl->next = vtophys(dp);
		sc->ch[chan]->rl->vnext = dp;
		sc->ch[chan]->rl->flags &= ~0x40000000;
		sc->ch[chan]->rl = dp;
	}
}


/*
 * Interupt handler
 */

static void
mn_intr(void *xsc)
{
	struct softc *sc;
	u_int32_t stat, lstat, u;
	int i, j;

	sc = xsc;
	stat =  sc->m32x->stat;
	lstat =  sc->m32x->lstat;
#if 0
	if (!stat && !(lstat & 2)) 
		return;
#endif

	if (stat & ~0xc200) {
		kprintf("%s: I stat=%08x lstat=%08x\n", sc->name, stat, lstat);
	}

	if ((stat & 0x200) || (lstat & 2)) 
		f54_intr(sc);

	for (j = i = 0; i < 64; i ++) {
		u = sc->riqb[i];
		if (u) {
			sc->riqb[i] = 0;
			mn_rx_intr(sc, u);
			if ((u & ~0x1f) == 0x30000800 || (u & ~0x1f) == 0x30000b00) 
				continue;
			u &= ~0x30000400;	/* bits we don't care about */
			if ((u & ~0x1f) == 0x00000900)
				continue;
			if (!(u & ~0x1f))
				continue;
			if (!j)
				kprintf("%s*: RIQB:", sc->name);
			kprintf(" [%d]=%08x", i, u);
			j++;
		}
	}
	if (j)
	    kprintf("\n");

	for (j = i = 0; i < 64; i ++) {
		u = sc->tiqb[i];
		if (u) {
			sc->tiqb[i] = 0;
			mn_tx_intr(sc, u);
			if ((u & ~0x1f) == 0x20000800)
				continue;
			u &= ~0x20000000;	/* bits we don't care about */
			if (!u)
				continue;
			if (!j)
				kprintf("%s*: TIQB:", sc->name);
			kprintf(" [%d]=%08x", i, u);
			j++;
		}
	}
	if (j)
		kprintf("\n");
	sc->m32x->stat = stat;
}

/*
 * PCI initialization stuff
 */

static int
mn_probe (device_t self)
{
	u_int id = pci_get_devid(self);

	if (sizeof (struct m32xreg) != 256) {
		kprintf("MN: sizeof(struct m32xreg) = %zd, should have been 256\n", sizeof (struct m32xreg));
		return (ENXIO);
	}
	if (sizeof (struct f54rreg) != 128) {
		kprintf("MN: sizeof(struct f54rreg) = %zd, should have been 128\n", sizeof (struct f54rreg));
		return (ENXIO);
	}
	if (sizeof (struct f54wreg) != 128) {
		kprintf("MN: sizeof(struct f54wreg) = %zd, should have been 128\n", sizeof (struct f54wreg));
		return (ENXIO);
	}

	if (id != 0x2101110a) 
		return (ENXIO);

	device_set_desc_copy(self, "Munich32X E1/T1 HDLC Controller");
	return (0);
}

static int
mn_attach (device_t self)
{
	struct softc *sc;
	u_int32_t u;
	u_int32_t ver;
	static int once;
	int rid, error;
	struct resource *res;

	if (!once) {
		if (ng_newtype(&mntypestruct))
			kprintf("ng_newtype failed\n");
		once++;
	}

	sc = (struct softc *)kmalloc(sizeof *sc, M_MN, M_WAITOK | M_ZERO);
	device_set_softc(self, sc);

	sc->dev = self;
	sc->unit = device_get_unit(self);
	sc->framing = E1;
	ksprintf(sc->name, "mn%d", sc->unit);

        rid = PCIR_MAPS;
        res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, RF_ACTIVE);
        if (res == NULL) {
                device_printf(self, "Could not map memory\n");
                return ENXIO;
        }
        sc->m0v = rman_get_virtual(res);
        sc->m0p = rman_get_start(res);

        rid = PCIR_MAPS + 4;
        res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, RF_ACTIVE);
        if (res == NULL) {
                device_printf(self, "Could not map memory\n");
                return ENXIO;
        }
        sc->m1v = rman_get_virtual(res);
        sc->m1p = rman_get_start(res);

	/* Allocate interrupt */
	rid = 0;
	sc->irq = bus_alloc_resource_any(self, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->irq == NULL) {
		kprintf("couldn't map interrupt\n");
		return(ENXIO);
	}

	error = bus_setup_intr(self, sc->irq, INTR_MPSAFE, mn_intr, sc, 
			       &sc->intrhand, NULL);

	if (error) {
		kprintf("couldn't set up irq\n");
		return(ENXIO);
	}

	u = pci_read_config(self, PCIR_COMMAND, 1);
	kprintf("%x\n", u);
	pci_write_config(self, PCIR_COMMAND, u | PCIM_CMD_PERRESPEN | PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN, 1);
#if 0
	pci_write_config(self, PCIR_COMMAND, 0x02800046, 4);
#endif
	u = pci_read_config(self, PCIR_COMMAND, 1);
	kprintf("%x\n", u);

	ver = pci_get_revid(self);

	sc->m32x = (struct m32xreg *) sc->m0v;
	sc->f54w = (struct f54wreg *) sc->m1v;
	sc->f54r = (struct f54rreg *) sc->m1v;

	/* We must reset before poking at FALC54 registers */
	u = mn_reset(sc);
	if (!u)
		return (0);

	kprintf("mn%d: Munich32X", sc->unit);
	switch (ver) {
	case 0x13:
		kprintf(" Rev 2.2");
		break;
	default:
		kprintf(" Rev 0x%x\n", ver);
	}
	kprintf(", Falc54");
	switch (sc->f54r->vstr) {
	case 0:
		kprintf(" Rev < 1.3\n");
		break;
	case 1:
		kprintf(" Rev 1.3\n");
		break;
	case 2:
		kprintf(" Rev 1.4\n");
		break;
	case 0x10:
		kprintf("-LH Rev 1.1\n");
		break;
	case 0x13:
		kprintf("-LH Rev 1.3\n");
		break;
	default:
		kprintf(" Rev 0x%x\n", sc->f54r->vstr);
	}

	if (ng_make_node_common(&mntypestruct, &sc->node) != 0) {
		kprintf("ng_make_node_common failed\n");
		return (0);
	}
	sc->node->private = sc;
	ksprintf(sc->nodename, "%s%d", NG_MN_NODE_TYPE, sc->unit);
	if (ng_name_node(sc->node, sc->nodename)) {
		ng_rmnode(sc->node);
		ng_unref(sc->node);
		return (0);
	}
	
	return (0);
}


static device_method_t mn_methods[] = {
        /* Device interface */
        DEVMETHOD(device_probe,         mn_probe),
        DEVMETHOD(device_attach,        mn_attach),
        DEVMETHOD(device_suspend,       bus_generic_suspend),
        DEVMETHOD(device_resume,        bus_generic_resume),
        DEVMETHOD(device_shutdown,      bus_generic_shutdown),

        DEVMETHOD_END
};
 
static driver_t mn_driver = {
        "mn",
        mn_methods,
        0
};

static devclass_t mn_devclass;

DECLARE_DUMMY_MODULE(if_mn);
DRIVER_MODULE(if_mn, pci, mn_driver, mn_devclass, NULL, NULL);
