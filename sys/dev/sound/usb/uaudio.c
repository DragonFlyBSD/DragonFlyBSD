/*	$NetBSD: uaudio.c,v 1.91 2004/11/05 17:46:14 kent Exp $	*/
/*	$FreeBSD: src/sys/dev/sound/usb/uaudio.c,v 1.14.2.2 2006/04/04 17:34:10 ariff Exp $ */

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * USB audio specs: http://www.usb.org/developers/devclass_docs/audio10.pdf
 *                  http://www.usb.org/developers/devclass_docs/frmts10.pdf
 *                  http://www.usb.org/developers/devclass_docs/termt10.pdf
 */

/*
 * Also merged:
 *  $NetBSD: uaudio.c,v 1.94 2005/01/15 15:19:53 kent Exp $
 *  $NetBSD: uaudio.c,v 1.95 2005/01/16 06:02:19 dsainty Exp $
 *  $NetBSD: uaudio.c,v 1.96 2005/01/16 12:46:00 kent Exp $
 *  $NetBSD: uaudio.c,v 1.97 2005/02/24 08:19:38 martin Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <sys/reboot.h>		/* for bootverbose */
#include <sys/select.h>
#include <sys/proc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/poll.h>
#include <sys/sysctl.h>
#include <sys/thread2.h>

#include <dev/sound/pcm/sound.h>	/* XXXXX */
#include <dev/sound/chip.h>
#include "feeder_if.h"

#include <bus/usb/usb.h>
#include <bus/usb/usbdi.h>
#include <bus/usb/usbdi_util.h>
#include <bus/usb/usb_quirks.h>

#include <dev/sound/usb/uaudioreg.h>
#include <dev/sound/usb/uaudio.h>

/* #define USB_DEBUG */
/* #define UAUDIO_MULTIPLE_ENDPOINTS */
#ifdef USB_DEBUG
#define DPRINTF(x)	do { if (uaudiodebug) kprintf x; } while (0)
#define DPRINTFN(n,x)	do { if (uaudiodebug>(n)) kprintf x; } while (0)
int	uaudiodebug = 0;
SYSCTL_NODE(_hw_usb, OID_AUTO, uaudio, CTLFLAG_RW, 0, "USB uaudio");
SYSCTL_INT(_hw_usb_uaudio, OID_AUTO, debug, CTLFLAG_RW,
	   &uaudiodebug, 0, "uaudio debug level");
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define UAUDIO_NCHANBUFS 6	/* number of outstanding request */
#define UAUDIO_NFRAMES   20	/* ms of sound in each request */


#define MIX_MAX_CHAN 8
struct mixerctl {
	uint16_t	wValue[MIX_MAX_CHAN]; /* using nchan */
	uint16_t	wIndex;
	uint8_t		nchan;
	uint8_t		type;
#define MIX_ON_OFF	1
#define MIX_SIGNED_16	2
#define MIX_UNSIGNED_16	3
#define MIX_SIGNED_8	4
#define MIX_SELECTOR	5
#define MIX_SIZE(n) ((n) == MIX_SIGNED_16 || (n) == MIX_UNSIGNED_16 ? 2 : 1)
#define MIX_UNSIGNED(n) ((n) == MIX_UNSIGNED_16)
	int		minval, maxval;
	u_int		delta;
	u_int		mul;
	unsigned	ctl;
#define MAX_SELECTOR_INPUT_PIN 256
	uint8_t		slctrtype[MAX_SELECTOR_INPUT_PIN];
	uint8_t		class;
};
#define MAKE(h,l) (((h) << 8) | (l))

struct as_info {
	uint8_t		alt;
	uint8_t		encoding;
	uint8_t		attributes; /* Copy of bmAttributes of
				     * usb_audio_streaming_endpoint_descriptor
				     */
	usbd_interface_handle	ifaceh;
	const usb_interface_descriptor_t *idesc;
	const usb_endpoint_descriptor_audio_t *edesc;
	const usb_endpoint_descriptor_audio_t *edesc1;
	const struct usb_audio_streaming_type1_descriptor *asf1desc;
	int		sc_busy;	/* currently used */
};

struct chan {
	struct pcm_channel *pcm_ch;
	usbd_pipe_handle pipe;
	usbd_pipe_handle sync_pipe;

	u_int	sample_size;
	u_int	sample_rate;
	u_int	bytes_per_frame;
	u_int	fraction;	/* fraction/1000 is the extra samples/frame */
	u_int	residue;	/* accumulates the fractional samples */

	u_char	*start;		/* upper layer buffer start */
	u_char	*end;		/* upper layer buffer end */
	u_char	*cur;		/* current position in upper layer buffer */
	int	blksize;	/* chunk size to report up */
	int	transferred;	/* transferred bytes not reported up */

	int	altidx;		/* currently used altidx */

	int	curchanbuf;
	struct chanbuf {
		struct chan	*chan;
		usbd_xfer_handle xfer;
		u_char		*buffer;
		u_int16_t	sizes[UAUDIO_NFRAMES];
		u_int16_t	offsets[UAUDIO_NFRAMES];
		u_int16_t	size;
	} chanbufs[UAUDIO_NCHANBUFS];

	struct uaudio_softc *sc; /* our softc */
	u_int32_t format;
	int	precision;
	int	channels;
};

struct uaudio_softc {
	device_t	sc_dev;		/* base device */
	usbd_device_handle sc_udev;	/* USB device */
	int		sc_ac_iface;	/* Audio Control interface */
	usbd_interface_handle	sc_ac_ifaceh;
	struct chan	sc_playchan;	/* play channel */
	struct chan	sc_recchan;	/* record channel */
	int		sc_nullalt;
	int		sc_audio_rev;
	struct as_info	*sc_alts;	/* alternate settings */
	int		sc_nalts;	/* # of alternate settings */
	int		sc_altflags;
#define HAS_8		0x01
#define HAS_16		0x02
#define HAS_8U		0x04
#define HAS_ALAW	0x08
#define HAS_MULAW	0x10
#define UA_NOFRAC	0x20		/* don't do sample rate adjustment */
#define HAS_24		0x40
#define HAS_32		0x80
	int		sc_mode;	/* play/record capability */
	struct mixerctl *sc_ctls;	/* mixer controls */
	int		sc_nctls;	/* # of mixer controls */
	device_t	sc_audiodev;
	char		sc_dying;
	struct sbuf	uaudio_sndstat;
	int		uaudio_sndstat_flag;
};

struct terminal_list {
	int size;
	uint16_t terminals[1];
};
#define TERMINAL_LIST_SIZE(N)	(offsetof(struct terminal_list, terminals) \
				+ sizeof(uint16_t) * (N))

struct io_terminal {
	union {
		const usb_descriptor_t *desc;
		const struct usb_audio_input_terminal *it;
		const struct usb_audio_output_terminal *ot;
		const struct usb_audio_mixer_unit *mu;
		const struct usb_audio_selector_unit *su;
		const struct usb_audio_feature_unit *fu;
		const struct usb_audio_processing_unit *pu;
		const struct usb_audio_extension_unit *eu;
	} d;
	int inputs_size;
	struct terminal_list **inputs; /* list of source input terminals */
	struct terminal_list *output; /* list of destination output terminals */
	int direct;		/* directly connected to an output terminal */
};

#define UAC_OUTPUT	0
#define UAC_INPUT	1
#define UAC_EQUAL	2
#define UAC_RECORD	3
#define UAC_NCLASSES	4
#ifdef USB_DEBUG
#define AudioCinputs	"inputs"
#define AudioCoutputs	"outputs"
#define AudioCrecord	"record"
#define AudioCequalization	"equalization"
static const char *uac_names[] = {
	AudioCoutputs, AudioCinputs, AudioCequalization, AudioCrecord,
};
#endif

static usbd_status uaudio_identify_ac
	(struct uaudio_softc *, const usb_config_descriptor_t *);
static usbd_status uaudio_identify_as
	(struct uaudio_softc *, const usb_config_descriptor_t *);
static usbd_status uaudio_process_as
	(struct uaudio_softc *, const char *, int *, int,
	 const usb_interface_descriptor_t *);

static void	uaudio_add_alt(struct uaudio_softc *, const struct as_info *);

static const usb_interface_descriptor_t *uaudio_find_iface
	(const char *, int, int *, int);

static void	uaudio_mixer_add_ctl(struct uaudio_softc *, struct mixerctl *);

#ifdef USB_DEBUG
static void	uaudio_dump_cluster(const struct usb_audio_cluster *);
#endif
static struct usb_audio_cluster uaudio_get_cluster
	(int, const struct io_terminal *);
static void	uaudio_add_input
	(struct uaudio_softc *, const struct io_terminal *, int);
static void	uaudio_add_output
	(struct uaudio_softc *, const struct io_terminal *, int);
static void	uaudio_add_mixer
	(struct uaudio_softc *, const struct io_terminal *, int);
static void	uaudio_add_selector
	(struct uaudio_softc *, const struct io_terminal *, int);
#ifdef USB_DEBUG
static const char *uaudio_get_terminal_name(int);
#endif
static int	uaudio_determine_class
	(const struct io_terminal *, struct mixerctl *);
static int	uaudio_feature_name(const struct io_terminal *,
		    struct mixerctl *);
static void	uaudio_add_feature
	(struct uaudio_softc *, const struct io_terminal *, int);
static void	uaudio_add_processing_updown
	(struct uaudio_softc *, const struct io_terminal *, int);
static void	uaudio_add_processing
	(struct uaudio_softc *, const struct io_terminal *, int);
static void	uaudio_add_extension
	(struct uaudio_softc *, const struct io_terminal *, int);
static struct terminal_list *uaudio_merge_terminal_list
	(const struct io_terminal *);
static struct terminal_list *uaudio_io_terminaltype
	(int, struct io_terminal *, int);
static usbd_status uaudio_identify
	(struct uaudio_softc *, const usb_config_descriptor_t *);

static int	uaudio_signext(int, int);
static int	uaudio_bsd2value(struct mixerctl *, int);
static int	uaudio_get(struct uaudio_softc *, int, int, int, int, int);
static void	uaudio_set
	(struct uaudio_softc *, int, int, int, int, int, int);
static void	uaudio_ctl_set
	(struct uaudio_softc *, int, struct mixerctl *, int, int);

static usbd_status uaudio_set_speed(struct uaudio_softc *, int, u_int);

static usbd_status uaudio_chan_open(struct uaudio_softc *, struct chan *);
static void	uaudio_chan_close(struct uaudio_softc *, struct chan *);
static usbd_status uaudio_chan_alloc_buffers
	(struct uaudio_softc *, struct chan *);
static void	uaudio_chan_free_buffers(struct uaudio_softc *, struct chan *);

static void	uaudio_chan_ptransfer(struct chan *);
static void	uaudio_chan_pintr
	(usbd_xfer_handle, usbd_private_handle, usbd_status);

static void	uaudio_chan_rtransfer(struct chan *);
static void	uaudio_chan_rintr
	(usbd_xfer_handle, usbd_private_handle, usbd_status);

static int	audio_attach_mi(device_t);
static int	uaudio_init_params(struct uaudio_softc * sc, struct chan *ch, int mode);
static int 	uaudio_sndstat_prepare_pcm(struct sbuf *s, device_t dev, int verbose);

/* for NetBSD compatibility */
#define	AUMODE_PLAY	0x01
#define	AUMODE_RECORD	0x02

#define	AUDIO_PROP_FULLDUPLEX	0x01

#define	AUDIO_ENCODING_ULAW		1
#define	AUDIO_ENCODING_ALAW		2
#define	AUDIO_ENCODING_SLINEAR_LE	6
#define	AUDIO_ENCODING_SLINEAR_BE	7
#define	AUDIO_ENCODING_ULINEAR_LE	8
#define	AUDIO_ENCODING_ULINEAR_BE	9

static device_probe_t uaudio_match;
static device_attach_t uaudio_attach;
static device_detach_t uaudio_detach;

static devclass_t uaudio_devclass;

static kobj_method_t uaudio_methods[] = {
	DEVMETHOD(device_probe, uaudio_match),
	DEVMETHOD(device_attach, uaudio_attach),
	DEVMETHOD(device_detach, uaudio_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),
	DEVMETHOD(bus_print_child, bus_generic_print_child),
	DEVMETHOD_END
};

static driver_t uaudio_driver = {
	"uaudio",
	uaudio_methods,
	sizeof(struct uaudio_softc)
};

MODULE_DEPEND(uaudio, usb, 1, 1, 1);

static int
uaudio_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usb_interface_descriptor_t *id;

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	id = usbd_get_interface_descriptor(uaa->iface);
	/* Trigger on the control interface. */
	if (id == NULL ||
	    id->bInterfaceClass != UICLASS_AUDIO ||
	    id->bInterfaceSubClass != UISUBCLASS_AUDIOCONTROL ||
	    (usbd_get_quirks(uaa->device)->uq_flags & UQ_BAD_AUDIO))
		return UMATCH_NONE;

	return UMATCH_IFACECLASS_IFACESUBCLASS;
}

static int
uaudio_attach(device_t self)
{
	struct uaudio_softc *sc = device_get_softc(self);
	struct usb_attach_arg *uaa = device_get_ivars(self);
	usb_interface_descriptor_t *id;
	usb_config_descriptor_t *cdesc;
	char devinfo[1024];
	usbd_status err;
	int i, j, found;

	usbd_devinfo(uaa->device, 0, devinfo);
	sc->sc_dev = self;
	device_set_desc_copy(self, devinfo);

	sc->sc_udev = uaa->device;

	cdesc = usbd_get_config_descriptor(sc->sc_udev);
	if (cdesc == NULL) {
		kprintf("%s: failed to get configuration descriptor\n",
		       device_get_nameunit(sc->sc_dev));
		return ENXIO;
	}

	err = uaudio_identify(sc, cdesc);
	if (err) {
		kprintf("%s: audio descriptors make no sense, error=%d\n",
		       device_get_nameunit(sc->sc_dev), err);
		return ENXIO;
	}

	sc->sc_ac_ifaceh = uaa->iface;
	/* Pick up the AS interface. */
	for (i = 0; i < uaa->nifaces; i++) {
		if (uaa->ifaces[i] == NULL)
			continue;
		id = usbd_get_interface_descriptor(uaa->ifaces[i]);
		if (id == NULL)
			continue;
		found = 0;
		for (j = 0; j < sc->sc_nalts; j++) {
			if (id->bInterfaceNumber ==
			    sc->sc_alts[j].idesc->bInterfaceNumber) {
				sc->sc_alts[j].ifaceh = uaa->ifaces[i];
				found = 1;
			}
		}
		if (found)
			uaa->ifaces[i] = NULL;
	}

	for (j = 0; j < sc->sc_nalts; j++) {
		if (sc->sc_alts[j].ifaceh == NULL) {
			kprintf("%s: alt %d missing AS interface(s)\n",
			    device_get_nameunit(sc->sc_dev), j);
			return ENXIO;
		}
	}

	kprintf("%s: audio rev %d.%02x\n", device_get_nameunit(sc->sc_dev),
	       sc->sc_audio_rev >> 8, sc->sc_audio_rev & 0xff);

	sc->sc_playchan.sc = sc->sc_recchan.sc = sc;
	sc->sc_playchan.altidx = -1;
	sc->sc_recchan.altidx = -1;

	if (usbd_get_quirks(sc->sc_udev)->uq_flags & UQ_AU_NO_FRAC)
		sc->sc_altflags |= UA_NOFRAC;

#ifndef USB_DEBUG
	if (bootverbose)
#endif
		kprintf("%s: %d mixer controls\n", device_get_nameunit(sc->sc_dev),
		    sc->sc_nctls);

	DPRINTF(("uaudio_attach: doing audio_attach_mi\n"));
	sc->sc_dying = 0;
	if (audio_attach_mi(sc->sc_dev)) {
		kprintf("audio_attach_mi failed\n");
		return ENXIO;
	}

	return 0;
}

static int
uaudio_detach(device_t self)
{
	struct uaudio_softc *sc = device_get_softc(self);

	sbuf_delete(&(sc->uaudio_sndstat));
	sc->uaudio_sndstat_flag = 0;

	sc->sc_dying = 1;

#if 0 /* XXX */
	/* Wait for outstanding requests to complete. */
	usbd_delay_ms(sc->sc_udev, UAUDIO_NCHANBUFS * UAUDIO_NFRAMES);
#endif

	/* do nothing ? */
	return bus_generic_detach(sc->sc_dev);
}

static const usb_interface_descriptor_t *
uaudio_find_iface(const char *buf, int size, int *offsp, int subtype)
{
	const usb_interface_descriptor_t *d;

	while (*offsp < size) {
		d = (const void *)(buf + *offsp);
		*offsp += d->bLength;
		if (d->bDescriptorType == UDESC_INTERFACE &&
		    d->bInterfaceClass == UICLASS_AUDIO &&
		    d->bInterfaceSubClass == subtype)
			return d;
	}
	return NULL;
}

static void
uaudio_mixer_add_ctl(struct uaudio_softc *sc, struct mixerctl *mc)
{
	int res;
	size_t len;
	struct mixerctl *nmc;

	if (mc->class < UAC_NCLASSES) {
		DPRINTF(("%s: adding %s.%d\n",
			 __func__, uac_names[mc->class], mc->ctl));
	} else {
		DPRINTF(("%s: adding %d\n", __func__, mc->ctl));
	}

	len = sizeof(*mc) * (sc->sc_nctls + 1);
	nmc = kmalloc(len, M_USBDEV, M_WAITOK);

	/* Copy old data, if there was any */
	if (sc->sc_nctls != 0) {
		memcpy(nmc, sc->sc_ctls, sizeof(*mc) * (sc->sc_nctls));
		kfree(sc->sc_ctls, M_USBDEV);
	}
	sc->sc_ctls = nmc;

	mc->delta = 0;
	if (mc->type == MIX_ON_OFF) {
		mc->minval = 0;
		mc->maxval = 1;
	} else if (mc->type == MIX_SELECTOR) {
		;
	} else {
		/* Determine min and max values. */
		mc->minval = uaudio_signext(mc->type,
			uaudio_get(sc, GET_MIN, UT_READ_CLASS_INTERFACE,
				   mc->wValue[0], mc->wIndex,
				   MIX_SIZE(mc->type)));
		mc->maxval = 1 + uaudio_signext(mc->type,
			uaudio_get(sc, GET_MAX, UT_READ_CLASS_INTERFACE,
				   mc->wValue[0], mc->wIndex,
				   MIX_SIZE(mc->type)));
		mc->mul = mc->maxval - mc->minval;
		if (mc->mul == 0)
			mc->mul = 1;
		res = uaudio_get(sc, GET_RES, UT_READ_CLASS_INTERFACE,
				 mc->wValue[0], mc->wIndex,
				 MIX_SIZE(mc->type));
		if (res > 0)
			mc->delta = (res * 255 + mc->mul/2) / mc->mul;
	}

	sc->sc_ctls[sc->sc_nctls++] = *mc;

#ifdef USB_DEBUG
	if (uaudiodebug > 2) {
		int i;
		DPRINTF(("uaudio_mixer_add_ctl: wValue=%04x",mc->wValue[0]));
		for (i = 1; i < mc->nchan; i++)
			DPRINTF((",%04x", mc->wValue[i]));
		DPRINTF((" wIndex=%04x type=%d ctl='%d' "
			 "min=%d max=%d\n",
			 mc->wIndex, mc->type, mc->ctl,
			 mc->minval, mc->maxval));
	}
#endif
}

#ifdef USB_DEBUG
static void
uaudio_dump_cluster(const struct usb_audio_cluster *cl)
{
	static const char *channel_names[16] = {
		"LEFT", "RIGHT", "CENTER", "LFE",
		"LEFT_SURROUND", "RIGHT_SURROUND", "LEFT_CENTER", "RIGHT_CENTER",
		"SURROUND", "LEFT_SIDE", "RIGHT_SIDE", "TOP",
		"RESERVED12", "RESERVED13", "RESERVED14", "RESERVED15",
	};
	int cc, i, first;

	cc = UGETW(cl->wChannelConfig);
	kprintf("cluster: bNrChannels=%u wChannelConfig=0x%.4x",
		  cl->bNrChannels, cc);
	first = TRUE;
	for (i = 0; cc != 0; i++) {
		if (cc & 1) {
			kprintf("%c%s", first ? '<' : ',', channel_names[i]);
			first = FALSE;
		}
		cc = cc >> 1;
	}
	kprintf("> iChannelNames=%u", cl->iChannelNames);
}
#endif

static struct usb_audio_cluster
uaudio_get_cluster(int id, const struct io_terminal *iot)
{
	struct usb_audio_cluster r;
	const usb_descriptor_t *dp;
	int i;

	for (i = 0; i < 25; i++) { /* avoid infinite loops */
		dp = iot[id].d.desc;
		if (dp == NULL)
			goto bad;
		switch (dp->bDescriptorSubtype) {
		case UDESCSUB_AC_INPUT:
			r.bNrChannels = iot[id].d.it->bNrChannels;
			USETW(r.wChannelConfig, UGETW(iot[id].d.it->wChannelConfig));
			r.iChannelNames = iot[id].d.it->iChannelNames;
			return r;
		case UDESCSUB_AC_OUTPUT:
			id = iot[id].d.ot->bSourceId;
			break;
		case UDESCSUB_AC_MIXER:
			r = *(const struct usb_audio_cluster *)
				&iot[id].d.mu->baSourceId[iot[id].d.mu->bNrInPins];
			return r;
		case UDESCSUB_AC_SELECTOR:
			/* XXX This is not really right */
			id = iot[id].d.su->baSourceId[0];
			break;
		case UDESCSUB_AC_FEATURE:
			id = iot[id].d.fu->bSourceId;
			break;
		case UDESCSUB_AC_PROCESSING:
			r = *(const struct usb_audio_cluster *)
				&iot[id].d.pu->baSourceId[iot[id].d.pu->bNrInPins];
			return r;
		case UDESCSUB_AC_EXTENSION:
			r = *(const struct usb_audio_cluster *)
				&iot[id].d.eu->baSourceId[iot[id].d.eu->bNrInPins];
			return r;
		default:
			goto bad;
		}
	}
 bad:
	kprintf("uaudio_get_cluster: bad data\n");
	memset(&r, 0, sizeof r);
	return r;

}

static void
uaudio_add_input(struct uaudio_softc *sc, const struct io_terminal *iot, int id)
{
#ifdef USB_DEBUG
	const struct usb_audio_input_terminal *d = iot[id].d.it;

	DPRINTFN(2,("uaudio_add_input: bTerminalId=%d wTerminalType=0x%04x "
		    "bAssocTerminal=%d bNrChannels=%d wChannelConfig=%d "
		    "iChannelNames=%d iTerminal=%d\n",
		    d->bTerminalId, UGETW(d->wTerminalType), d->bAssocTerminal,
		    d->bNrChannels, UGETW(d->wChannelConfig),
		    d->iChannelNames, d->iTerminal));
#endif
}

static void
uaudio_add_output(struct uaudio_softc *sc, const struct io_terminal *iot, int id)
{
#ifdef USB_DEBUG
	const struct usb_audio_output_terminal *d;

	d = iot[id].d.ot;
	DPRINTFN(2,("uaudio_add_output: bTerminalId=%d wTerminalType=0x%04x "
		    "bAssocTerminal=%d bSourceId=%d iTerminal=%d\n",
		    d->bTerminalId, UGETW(d->wTerminalType), d->bAssocTerminal,
		    d->bSourceId, d->iTerminal));
#endif
}

static void
uaudio_add_mixer(struct uaudio_softc *sc, const struct io_terminal *iot, int id)
{
	const struct usb_audio_mixer_unit *d = iot[id].d.mu;
	const struct usb_audio_mixer_unit_1 *d1;
	int c, chs, ichs, ochs, i, o, bno, p, mo, mc, k;
	const uByte *bm;
	struct mixerctl mix;

	DPRINTFN(2,("uaudio_add_mixer: bUnitId=%d bNrInPins=%d\n",
		    d->bUnitId, d->bNrInPins));

	/* Compute the number of input channels */
	ichs = 0;
	for (i = 0; i < d->bNrInPins; i++)
		ichs += uaudio_get_cluster(d->baSourceId[i], iot).bNrChannels;

	/* and the number of output channels */
	d1 = (const struct usb_audio_mixer_unit_1 *)&d->baSourceId[d->bNrInPins];
	ochs = d1->bNrChannels;
	DPRINTFN(2,("uaudio_add_mixer: ichs=%d ochs=%d\n", ichs, ochs));

	bm = d1->bmControls;
	mix.wIndex = MAKE(d->bUnitId, sc->sc_ac_iface);
	uaudio_determine_class(&iot[id], &mix);
	mix.type = MIX_SIGNED_16;

#define BIT(bno) ((bm[bno / 8] >> (7 - bno % 8)) & 1)
	for (p = i = 0; i < d->bNrInPins; i++) {
		chs = uaudio_get_cluster(d->baSourceId[i], iot).bNrChannels;
		mc = 0;
		for (c = 0; c < chs; c++) {
			mo = 0;
			for (o = 0; o < ochs; o++) {
				bno = (p + c) * ochs + o;
				if (BIT(bno))
					mo++;
			}
			if (mo == 1)
				mc++;
		}
		if (mc == chs && chs <= MIX_MAX_CHAN) {
			k = 0;
			for (c = 0; c < chs; c++)
				for (o = 0; o < ochs; o++) {
					bno = (p + c) * ochs + o;
					if (BIT(bno))
						mix.wValue[k++] =
							MAKE(p+c+1, o+1);
				}

			mix.nchan = chs;
			uaudio_mixer_add_ctl(sc, &mix);
		} else {
			/* XXX */
		}
#undef BIT
		p += chs;
	}

}

static void
uaudio_add_selector(struct uaudio_softc *sc, const struct io_terminal *iot, int id)
{
	const struct usb_audio_selector_unit *d;
	struct mixerctl mix;
	int i;
	struct mixerctl dummy;

	d = iot[id].d.su;
	DPRINTFN(2,("uaudio_add_selector: bUnitId=%d bNrInPins=%d\n",
		    d->bUnitId, d->bNrInPins));
	mix.wIndex = MAKE(d->bUnitId, sc->sc_ac_iface);
	mix.wValue[0] = MAKE(0, 0);
	uaudio_determine_class(&iot[id], &mix);
	mix.nchan = 1;
	mix.type = MIX_SELECTOR;
	mix.ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
	mix.minval = 1;
	mix.maxval = d->bNrInPins;
	mix.mul = mix.maxval - mix.minval;
	for (i = 0; i < MAX_SELECTOR_INPUT_PIN; i++) {
		mix.slctrtype[i] = SOUND_MIXER_NRDEVICES;
	}
	for (i = mix.minval; i <= mix.maxval; i++) {
		mix.slctrtype[i - 1] = uaudio_feature_name(&iot[d->baSourceId[i - 1]], &dummy);
	}

	uaudio_mixer_add_ctl(sc, &mix);
}

#ifdef USB_DEBUG
static const char *
uaudio_get_terminal_name(int terminal_type)
{
	static char buf[100];

	switch (terminal_type) {
	/* USB terminal types */
	case UAT_UNDEFINED:	return "UAT_UNDEFINED";
	case UAT_STREAM:	return "UAT_STREAM";
	case UAT_VENDOR:	return "UAT_VENDOR";
	/* input terminal types */
	case UATI_UNDEFINED:	return "UATI_UNDEFINED";
	case UATI_MICROPHONE:	return "UATI_MICROPHONE";
	case UATI_DESKMICROPHONE:	return "UATI_DESKMICROPHONE";
	case UATI_PERSONALMICROPHONE:	return "UATI_PERSONALMICROPHONE";
	case UATI_OMNIMICROPHONE:	return "UATI_OMNIMICROPHONE";
	case UATI_MICROPHONEARRAY:	return "UATI_MICROPHONEARRAY";
	case UATI_PROCMICROPHONEARR:	return "UATI_PROCMICROPHONEARR";
	/* output terminal types */
	case UATO_UNDEFINED:	return "UATO_UNDEFINED";
	case UATO_SPEAKER:	return "UATO_SPEAKER";
	case UATO_HEADPHONES:	return "UATO_HEADPHONES";
	case UATO_DISPLAYAUDIO:	return "UATO_DISPLAYAUDIO";
	case UATO_DESKTOPSPEAKER:	return "UATO_DESKTOPSPEAKER";
	case UATO_ROOMSPEAKER:	return "UATO_ROOMSPEAKER";
	case UATO_COMMSPEAKER:	return "UATO_COMMSPEAKER";
	case UATO_SUBWOOFER:	return "UATO_SUBWOOFER";
	/* bidir terminal types */
	case UATB_UNDEFINED:	return "UATB_UNDEFINED";
	case UATB_HANDSET:	return "UATB_HANDSET";
	case UATB_HEADSET:	return "UATB_HEADSET";
	case UATB_SPEAKERPHONE:	return "UATB_SPEAKERPHONE";
	case UATB_SPEAKERPHONEESUP:	return "UATB_SPEAKERPHONEESUP";
	case UATB_SPEAKERPHONEECANC:	return "UATB_SPEAKERPHONEECANC";
	/* telephony terminal types */
	case UATT_UNDEFINED:	return "UATT_UNDEFINED";
	case UATT_PHONELINE:	return "UATT_PHONELINE";
	case UATT_TELEPHONE:	return "UATT_TELEPHONE";
	case UATT_DOWNLINEPHONE:	return "UATT_DOWNLINEPHONE";
	/* external terminal types */
	case UATE_UNDEFINED:	return "UATE_UNDEFINED";
	case UATE_ANALOGCONN:	return "UATE_ANALOGCONN";
	case UATE_LINECONN:	return "UATE_LINECONN";
	case UATE_LEGACYCONN:	return "UATE_LEGACYCONN";
	case UATE_DIGITALAUIFC:	return "UATE_DIGITALAUIFC";
	case UATE_SPDIF:	return "UATE_SPDIF";
	case UATE_1394DA:	return "UATE_1394DA";
	case UATE_1394DV:	return "UATE_1394DV";
	/* embedded function terminal types */
	case UATF_UNDEFINED:	return "UATF_UNDEFINED";
	case UATF_CALIBNOISE:	return "UATF_CALIBNOISE";
	case UATF_EQUNOISE:	return "UATF_EQUNOISE";
	case UATF_CDPLAYER:	return "UATF_CDPLAYER";
	case UATF_DAT:	return "UATF_DAT";
	case UATF_DCC:	return "UATF_DCC";
	case UATF_MINIDISK:	return "UATF_MINIDISK";
	case UATF_ANALOGTAPE:	return "UATF_ANALOGTAPE";
	case UATF_PHONOGRAPH:	return "UATF_PHONOGRAPH";
	case UATF_VCRAUDIO:	return "UATF_VCRAUDIO";
	case UATF_VIDEODISCAUDIO:	return "UATF_VIDEODISCAUDIO";
	case UATF_DVDAUDIO:	return "UATF_DVDAUDIO";
	case UATF_TVTUNERAUDIO:	return "UATF_TVTUNERAUDIO";
	case UATF_SATELLITE:	return "UATF_SATELLITE";
	case UATF_CABLETUNER:	return "UATF_CABLETUNER";
	case UATF_DSS:	return "UATF_DSS";
	case UATF_RADIORECV:	return "UATF_RADIORECV";
	case UATF_RADIOXMIT:	return "UATF_RADIOXMIT";
	case UATF_MULTITRACK:	return "UATF_MULTITRACK";
	case UATF_SYNTHESIZER:	return "UATF_SYNTHESIZER";
	default:
		ksnprintf(buf, sizeof(buf), "unknown type (0x%.4x)", terminal_type);
		return buf;
	}
}
#endif

static int
uaudio_determine_class(const struct io_terminal *iot, struct mixerctl *mix)
{
	int terminal_type;

	if (iot == NULL || iot->output == NULL) {
		mix->class = UAC_OUTPUT;
		return 0;
	}
	terminal_type = 0;
	if (iot->output->size == 1)
		terminal_type = iot->output->terminals[0];
	/*
	 * If the only output terminal is USB,
	 * the class is UAC_RECORD.
	 */
	if ((terminal_type & 0xff00) == (UAT_UNDEFINED & 0xff00)) {
		mix->class = UAC_RECORD;
		if (iot->inputs_size == 1
		    && iot->inputs[0] != NULL
		    && iot->inputs[0]->size == 1)
			return iot->inputs[0]->terminals[0];
		else
			return 0;
	}
	/*
	 * If the ultimate destination of the unit is just one output
	 * terminal and the unit is connected to the output terminal
	 * directly, the class is UAC_OUTPUT.
	 */
	if (terminal_type != 0 && iot->direct) {
		mix->class = UAC_OUTPUT;
		return terminal_type;
	}
	/*
	 * If the unit is connected to just one input terminal,
	 * the class is UAC_INPUT.
	 */
	if (iot->inputs_size == 1 && iot->inputs[0] != NULL
	    && iot->inputs[0]->size == 1) {
		mix->class = UAC_INPUT;
		return iot->inputs[0]->terminals[0];
	}
	/*
	 * Otherwise, the class is UAC_OUTPUT.
	 */
	mix->class = UAC_OUTPUT;
	return terminal_type;
}

static int 
uaudio_feature_name(const struct io_terminal *iot, struct mixerctl *mix)
{
	int terminal_type;

	terminal_type = uaudio_determine_class(iot, mix);
	if (mix->class == UAC_RECORD && terminal_type == 0)
		return SOUND_MIXER_IMIX;
	DPRINTF(("%s: terminal_type=%s\n", __func__,
		 uaudio_get_terminal_name(terminal_type)));
	switch (terminal_type) {
	case UAT_STREAM:
		return SOUND_MIXER_PCM;

	case UATI_MICROPHONE:
	case UATI_DESKMICROPHONE:
	case UATI_PERSONALMICROPHONE:
	case UATI_OMNIMICROPHONE:
	case UATI_MICROPHONEARRAY:
	case UATI_PROCMICROPHONEARR:
		return SOUND_MIXER_MIC;

	case UATO_SPEAKER:
	case UATO_DESKTOPSPEAKER:
	case UATO_ROOMSPEAKER:
	case UATO_COMMSPEAKER:
		return SOUND_MIXER_SPEAKER;

	case UATE_ANALOGCONN:
	case UATE_LINECONN:
	case UATE_LEGACYCONN:
		return SOUND_MIXER_LINE;

	case UATE_DIGITALAUIFC:
	case UATE_SPDIF:
	case UATE_1394DA:
	case UATE_1394DV:
		return SOUND_MIXER_ALTPCM;

	case UATF_CDPLAYER:
		return SOUND_MIXER_CD;

	case UATF_SYNTHESIZER:
		return SOUND_MIXER_SYNTH;

	case UATF_VIDEODISCAUDIO:
	case UATF_DVDAUDIO:
	case UATF_TVTUNERAUDIO:
		return SOUND_MIXER_VIDEO;

/* telephony terminal types */
	case UATT_UNDEFINED:
	case UATT_PHONELINE:
	case UATT_TELEPHONE:
	case UATT_DOWNLINEPHONE:
		return SOUND_MIXER_PHONEIN;
/*		return SOUND_MIXER_PHONEOUT;*/

	case UATF_RADIORECV:
	case UATF_RADIOXMIT:
		return SOUND_MIXER_RADIO;

	case UAT_UNDEFINED:
	case UAT_VENDOR:
	case UATI_UNDEFINED:
/* output terminal types */
	case UATO_UNDEFINED:
	case UATO_DISPLAYAUDIO:
	case UATO_SUBWOOFER:
	case UATO_HEADPHONES:
/* bidir terminal types */
	case UATB_UNDEFINED:
	case UATB_HANDSET:
	case UATB_HEADSET:
	case UATB_SPEAKERPHONE:
	case UATB_SPEAKERPHONEESUP:
	case UATB_SPEAKERPHONEECANC:
/* external terminal types */
	case UATE_UNDEFINED:
/* embedded function terminal types */
	case UATF_UNDEFINED:
	case UATF_CALIBNOISE:
	case UATF_EQUNOISE:
	case UATF_DAT:
	case UATF_DCC:
	case UATF_MINIDISK:
	case UATF_ANALOGTAPE:
	case UATF_PHONOGRAPH:
	case UATF_VCRAUDIO:
	case UATF_SATELLITE:
	case UATF_CABLETUNER:
	case UATF_DSS:
	case UATF_MULTITRACK:
	case 0xffff:
	default:
		DPRINTF(("%s: 'master' for 0x%.4x\n", __func__, terminal_type));
		return SOUND_MIXER_VOLUME;
	}
	return SOUND_MIXER_VOLUME;
}

static void
uaudio_add_feature(struct uaudio_softc *sc, const struct io_terminal *iot, int id)
{
	const struct usb_audio_feature_unit *d;
	const uByte *ctls;
	int ctlsize;
	int nchan;
	u_int fumask, mmask, cmask;
	struct mixerctl mix;
	int chan, ctl, i, unit;
	int mixernumber;

#define GET(i) (ctls[(i)*ctlsize] | \
		(ctlsize > 1 ? ctls[(i)*ctlsize+1] << 8 : 0))
	d = iot[id].d.fu;
	ctls = d->bmaControls;
	ctlsize = d->bControlSize;
	nchan = (d->bLength - 7) / ctlsize;
	mmask = GET(0);
	/* Figure out what we can control */
	for (cmask = 0, chan = 1; chan < nchan; chan++) {
		DPRINTFN(9,("uaudio_add_feature: chan=%d mask=%x\n",
			    chan, GET(chan)));
		cmask |= GET(chan);
	}

	if (nchan > MIX_MAX_CHAN)
		nchan = MIX_MAX_CHAN;
	unit = d->bUnitId;
	mix.wIndex = MAKE(unit, sc->sc_ac_iface);
	for (ctl = MUTE_CONTROL; ctl < LOUDNESS_CONTROL; ctl++) {
		fumask = FU_MASK(ctl);
		DPRINTFN(4,("uaudio_add_feature: ctl=%d fumask=0x%04x\n",
			    ctl, fumask));
		if (mmask & fumask) {
			mix.nchan = 1;
			mix.wValue[0] = MAKE(ctl, 0);
		} else if (cmask & fumask) {
			mix.nchan = nchan - 1;
			for (i = 1; i < nchan; i++) {
				if (GET(i) & fumask)
					mix.wValue[i-1] = MAKE(ctl, i);
				else
					mix.wValue[i-1] = -1;
			}
		} else {
			continue;
		}
#undef GET

		mixernumber = uaudio_feature_name(&iot[id], &mix);

		switch (ctl) {
		case MUTE_CONTROL:
			mix.type = MIX_ON_OFF;
			mix.ctl = SOUND_MIXER_NRDEVICES;
			break;
		case VOLUME_CONTROL:
			mix.type = MIX_SIGNED_16;
			mix.ctl = mixernumber;
			break;
		case BASS_CONTROL:
			mix.type = MIX_SIGNED_8;
			mix.ctl = SOUND_MIXER_BASS;
			break;
		case MID_CONTROL:
			mix.type = MIX_SIGNED_8;
			mix.ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			break;
		case TREBLE_CONTROL:
			mix.type = MIX_SIGNED_8;
			mix.ctl = SOUND_MIXER_TREBLE;
			break;
		case GRAPHIC_EQUALIZER_CONTROL:
			continue; /* XXX don't add anything */
			break;
		case AGC_CONTROL:
			mix.type = MIX_ON_OFF;
			mix.ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			break;
		case DELAY_CONTROL:
			mix.type = MIX_UNSIGNED_16;
			mix.ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			break;
		case BASS_BOOST_CONTROL:
			mix.type = MIX_ON_OFF;
			mix.ctl = SOUND_MIXER_NRDEVICES;	/* XXXXX */
			break;
		case LOUDNESS_CONTROL:
			mix.type = MIX_ON_OFF;
			mix.ctl = SOUND_MIXER_LOUD;	/* Is this correct ? */
			break;
		}
		uaudio_mixer_add_ctl(sc, &mix);
	}
}

static void
uaudio_add_processing_updown(struct uaudio_softc *sc,
			     const struct io_terminal *iot, int id)
{
	const struct usb_audio_processing_unit *d;
	const struct usb_audio_processing_unit_1 *d1;
	const struct usb_audio_processing_unit_updown *ud;
	struct mixerctl mix;
	int i;

	d = iot[id].d.pu;
	d1 = (const struct usb_audio_processing_unit_1 *)
		&d->baSourceId[d->bNrInPins];
	ud = (const struct usb_audio_processing_unit_updown *)
		&d1->bmControls[d1->bControlSize];
	DPRINTFN(2,("uaudio_add_processing_updown: bUnitId=%d bNrModes=%d\n",
		    d->bUnitId, ud->bNrModes));

	if (!(d1->bmControls[0] & UA_PROC_MASK(UD_MODE_SELECT_CONTROL))) {
		DPRINTF(("uaudio_add_processing_updown: no mode select\n"));
		return;
	}

	mix.wIndex = MAKE(d->bUnitId, sc->sc_ac_iface);
	mix.nchan = 1;
	mix.wValue[0] = MAKE(UD_MODE_SELECT_CONTROL, 0);
	uaudio_determine_class(&iot[id], &mix);
	mix.type = MIX_ON_OFF;	/* XXX */

	for (i = 0; i < ud->bNrModes; i++) {
		DPRINTFN(2,("uaudio_add_processing_updown: i=%d bm=0x%x\n",
			    i, UGETW(ud->waModes[i])));
		/* XXX */
	}
	uaudio_mixer_add_ctl(sc, &mix);
}

static void
uaudio_add_processing(struct uaudio_softc *sc, const struct io_terminal *iot, int id)
{
	const struct usb_audio_processing_unit *d;
	const struct usb_audio_processing_unit_1 *d1;
	int ptype;
	struct mixerctl mix;

	d = iot[id].d.pu;
	d1 = (const struct usb_audio_processing_unit_1 *)
		&d->baSourceId[d->bNrInPins];
	ptype = UGETW(d->wProcessType);
	DPRINTFN(2,("uaudio_add_processing: wProcessType=%d bUnitId=%d "
		    "bNrInPins=%d\n", ptype, d->bUnitId, d->bNrInPins));

	if (d1->bmControls[0] & UA_PROC_ENABLE_MASK) {
		mix.wIndex = MAKE(d->bUnitId, sc->sc_ac_iface);
		mix.nchan = 1;
		mix.wValue[0] = MAKE(XX_ENABLE_CONTROL, 0);
		uaudio_determine_class(&iot[id], &mix);
		mix.type = MIX_ON_OFF;
		uaudio_mixer_add_ctl(sc, &mix);
	}

	switch(ptype) {
	case UPDOWNMIX_PROCESS:
		uaudio_add_processing_updown(sc, iot, id);
		break;
	case DOLBY_PROLOGIC_PROCESS:
	case P3D_STEREO_EXTENDER_PROCESS:
	case REVERBATION_PROCESS:
	case CHORUS_PROCESS:
	case DYN_RANGE_COMP_PROCESS:
	default:
#ifdef USB_DEBUG
		kprintf("uaudio_add_processing: unit %d, type=%d not impl.\n",
		       d->bUnitId, ptype);
#endif
		break;
	}
}

static void
uaudio_add_extension(struct uaudio_softc *sc, const struct io_terminal *iot, int id)
{
	const struct usb_audio_extension_unit *d;
	const struct usb_audio_extension_unit_1 *d1;
	struct mixerctl mix;

	d = iot[id].d.eu;
	d1 = (const struct usb_audio_extension_unit_1 *)
		&d->baSourceId[d->bNrInPins];
	DPRINTFN(2,("uaudio_add_extension: bUnitId=%d bNrInPins=%d\n",
		    d->bUnitId, d->bNrInPins));

	if (usbd_get_quirks(sc->sc_udev)->uq_flags & UQ_AU_NO_XU)
		return;

	if (d1->bmControls[0] & UA_EXT_ENABLE_MASK) {
		mix.wIndex = MAKE(d->bUnitId, sc->sc_ac_iface);
		mix.nchan = 1;
		mix.wValue[0] = MAKE(UA_EXT_ENABLE, 0);
		uaudio_determine_class(&iot[id], &mix);
		mix.type = MIX_ON_OFF;
		uaudio_mixer_add_ctl(sc, &mix);
	}
}

static struct terminal_list*
uaudio_merge_terminal_list(const struct io_terminal *iot)
{
	struct terminal_list *tml;
	uint16_t *ptm;
	int i, len;

	len = 0;
	if (iot->inputs == NULL)
		return NULL;
	for (i = 0; i < iot->inputs_size; i++) {
		if (iot->inputs[i] != NULL)
			len += iot->inputs[i]->size;
	}
	tml = kmalloc(TERMINAL_LIST_SIZE(len), M_TEMP, M_WAITOK);
	tml->size = 0;
	ptm = tml->terminals;
	for (i = 0; i < iot->inputs_size; i++) {
		if (iot->inputs[i] == NULL)
			continue;
		if (iot->inputs[i]->size > len)
			break;
		memcpy(ptm, iot->inputs[i]->terminals,
		       iot->inputs[i]->size * sizeof(uint16_t));
		tml->size += iot->inputs[i]->size;
		ptm += iot->inputs[i]->size;
		len -= iot->inputs[i]->size;
	}
	return tml;
}

static struct terminal_list *
uaudio_io_terminaltype(int outtype, struct io_terminal *iot, int id)
{
	struct terminal_list *tml;
	struct io_terminal *it;
	int src_id, i;

	it = &iot[id];
	if (it->output != NULL) {
		/* already has outtype? */
		for (i = 0; i < it->output->size; i++)
			if (it->output->terminals[i] == outtype)
				return uaudio_merge_terminal_list(it);
		tml = kmalloc(TERMINAL_LIST_SIZE(it->output->size + 1),
			     M_TEMP, M_WAITOK);
		memcpy(tml, it->output, TERMINAL_LIST_SIZE(it->output->size));
		tml->terminals[it->output->size] = outtype;
		tml->size++;
		kfree(it->output, M_TEMP);
		it->output = tml;
		if (it->inputs != NULL) {
			for (i = 0; i < it->inputs_size; i++)
				if (it->inputs[i] != NULL)
					kfree(it->inputs[i], M_TEMP);
			kfree(it->inputs, M_TEMP);
		}
		it->inputs_size = 0;
		it->inputs = NULL;
	} else {		/* end `iot[id] != NULL' */
		it->inputs_size = 0;
		it->inputs = NULL;
		it->output = kmalloc(TERMINAL_LIST_SIZE(1), M_TEMP, M_WAITOK);
		it->output->terminals[0] = outtype;
		it->output->size = 1;
		it->direct = FALSE;
	}

	switch (it->d.desc->bDescriptorSubtype) {
	case UDESCSUB_AC_INPUT:
		it->inputs = kmalloc(sizeof(struct terminal_list *),
				     M_TEMP, M_WAITOK);
		tml = kmalloc(TERMINAL_LIST_SIZE(1), M_TEMP, M_WAITOK);
		it->inputs[0] = tml;
		tml->terminals[0] = UGETW(it->d.it->wTerminalType);
		tml->size = 1;
		it->inputs_size = 1;
		return uaudio_merge_terminal_list(it);
	case UDESCSUB_AC_FEATURE:
		src_id = it->d.fu->bSourceId;
		it->inputs = kmalloc(sizeof(struct terminal_list *),
				     M_TEMP, M_WAITOK);
		it->inputs[0] = uaudio_io_terminaltype(outtype, iot, src_id);
		it->inputs_size = 1;
		return uaudio_merge_terminal_list(it);
	case UDESCSUB_AC_OUTPUT:
		it->inputs = kmalloc(sizeof(struct terminal_list *),
				     M_TEMP, M_WAITOK);
		src_id = it->d.ot->bSourceId;
		it->inputs[0] = uaudio_io_terminaltype(outtype, iot, src_id);
		it->inputs_size = 1;
		iot[src_id].direct = TRUE;
		return NULL;
	case UDESCSUB_AC_MIXER:
		it->inputs_size = 0;
		it->inputs = kmalloc(sizeof(struct terminal_list *) *
				     it->d.mu->bNrInPins, M_TEMP, M_WAITOK);
		for (i = 0; i < it->d.mu->bNrInPins; i++) {
			src_id = it->d.mu->baSourceId[i];
			it->inputs[i] = uaudio_io_terminaltype(outtype, iot,
							       src_id);
			it->inputs_size++;
		}
		return uaudio_merge_terminal_list(it);
	case UDESCSUB_AC_SELECTOR:
		it->inputs_size = 0;
		it->inputs = kmalloc(sizeof(struct terminal_list *) *
				     it->d.su->bNrInPins, M_TEMP, M_WAITOK);
		for (i = 0; i < it->d.su->bNrInPins; i++) {
			src_id = it->d.su->baSourceId[i];
			it->inputs[i] = uaudio_io_terminaltype(outtype, iot,
							       src_id);
			it->inputs_size++;
		}
		return uaudio_merge_terminal_list(it);
	case UDESCSUB_AC_PROCESSING:
		it->inputs_size = 0;
		it->inputs = kmalloc(sizeof(struct terminal_list *) *
				     it->d.pu->bNrInPins, M_TEMP, M_WAITOK);
		for (i = 0; i < it->d.pu->bNrInPins; i++) {
			src_id = it->d.pu->baSourceId[i];
			it->inputs[i] = uaudio_io_terminaltype(outtype, iot,
							       src_id);
			it->inputs_size++;
		}
		return uaudio_merge_terminal_list(it);
	case UDESCSUB_AC_EXTENSION:
		it->inputs_size = 0;
		it->inputs = kmalloc(sizeof(struct terminal_list *) *
				     it->d.eu->bNrInPins, M_TEMP, M_WAITOK);
		for (i = 0; i < it->d.eu->bNrInPins; i++) {
			src_id = it->d.eu->baSourceId[i];
			it->inputs[i] = uaudio_io_terminaltype(outtype, iot,
							       src_id);
			it->inputs_size++;
		}
		return uaudio_merge_terminal_list(it);
	case UDESCSUB_AC_HEADER:
	default:
		return NULL;
	}
}

static usbd_status
uaudio_identify(struct uaudio_softc *sc, const usb_config_descriptor_t *cdesc)
{
	usbd_status err;

	err = uaudio_identify_ac(sc, cdesc);
	if (err)
		return err;
	return uaudio_identify_as(sc, cdesc);
}

static void
uaudio_add_alt(struct uaudio_softc *sc, const struct as_info *ai)
{
	size_t len;
	struct as_info *nai;

	len = sizeof(*ai) * (sc->sc_nalts + 1);
	nai = kmalloc(len, M_USBDEV, M_WAITOK);

	/* Copy old data, if there was any */
	if (sc->sc_nalts != 0) {
		memcpy(nai, sc->sc_alts, sizeof(*ai) * (sc->sc_nalts));
		kfree(sc->sc_alts, M_USBDEV);
	}
	sc->sc_alts = nai;
	DPRINTFN(2,("uaudio_add_alt: adding alt=%d, enc=%d\n",
		    ai->alt, ai->encoding));
	sc->sc_alts[sc->sc_nalts++] = *ai;
}

static usbd_status
uaudio_process_as(struct uaudio_softc *sc, const char *buf, int *offsp,
		  int size, const usb_interface_descriptor_t *id)
#define offs (*offsp)
{
	const struct usb_audio_streaming_interface_descriptor *asid;
	const struct usb_audio_streaming_type1_descriptor *asf1d;
	const usb_endpoint_descriptor_audio_t *ed;
	const usb_endpoint_descriptor_audio_t *epdesc1;
	const struct usb_audio_streaming_endpoint_descriptor *sed;
	int format, chan, prec, enc;
	int dir, type, sync;
	struct as_info ai;
	const char *format_str;

	asid = (const void *)(buf + offs);

	if (asid->bDescriptorType != UDESC_CS_INTERFACE ||
	    asid->bDescriptorSubtype != AS_GENERAL)
		return USBD_INVAL;
	DPRINTF(("uaudio_process_as: asid: bTerminakLink=%d wFormatTag=%d\n",
		 asid->bTerminalLink, UGETW(asid->wFormatTag)));
	offs += asid->bLength;
	if (offs > size)
		return USBD_INVAL;

	asf1d = (const void *)(buf + offs);
	if (asf1d->bDescriptorType != UDESC_CS_INTERFACE ||
	    asf1d->bDescriptorSubtype != FORMAT_TYPE)
		return USBD_INVAL;
	offs += asf1d->bLength;
	if (offs > size)
		return USBD_INVAL;

	if (asf1d->bFormatType != FORMAT_TYPE_I) {
		kprintf("%s: ignored setting with type %d format\n",
		       device_get_nameunit(sc->sc_dev), UGETW(asid->wFormatTag));
		return USBD_NORMAL_COMPLETION;
	}

	ed = (const void *)(buf + offs);
	if (ed->bDescriptorType != UDESC_ENDPOINT)
		return USBD_INVAL;
	DPRINTF(("uaudio_process_as: endpoint[0] bLength=%d bDescriptorType=%d "
		 "bEndpointAddress=%d bmAttributes=0x%x wMaxPacketSize=%d "
		 "bInterval=%d bRefresh=%d bSynchAddress=%d\n",
		 ed->bLength, ed->bDescriptorType, ed->bEndpointAddress,
		 ed->bmAttributes, UGETW(ed->wMaxPacketSize),
		 ed->bInterval, ed->bRefresh, ed->bSynchAddress));
	offs += ed->bLength;
	if (offs > size)
		return USBD_INVAL;
	if (UE_GET_XFERTYPE(ed->bmAttributes) != UE_ISOCHRONOUS)
		return USBD_INVAL;

	dir = UE_GET_DIR(ed->bEndpointAddress);
	type = UE_GET_ISO_TYPE(ed->bmAttributes);
	if ((usbd_get_quirks(sc->sc_udev)->uq_flags & UQ_AU_INP_ASYNC) &&
	    dir == UE_DIR_IN && type == UE_ISO_ADAPT)
		type = UE_ISO_ASYNC;

	/* We can't handle endpoints that need a sync pipe yet. */
	sync = FALSE;
	if (dir == UE_DIR_IN && type == UE_ISO_ADAPT) {
		sync = TRUE;
#ifndef UAUDIO_MULTIPLE_ENDPOINTS
		kprintf("%s: ignored input endpoint of type adaptive\n",
		       device_get_nameunit(sc->sc_dev));
		return USBD_NORMAL_COMPLETION;
#endif
	}
	if (dir != UE_DIR_IN && type == UE_ISO_ASYNC) {
		sync = TRUE;
#ifndef UAUDIO_MULTIPLE_ENDPOINTS
		kprintf("%s: ignored output endpoint of type async\n",
		       device_get_nameunit(sc->sc_dev));
		return USBD_NORMAL_COMPLETION;
#endif
	}

	sed = (const void *)(buf + offs);
	if (sed->bDescriptorType != UDESC_CS_ENDPOINT ||
	    sed->bDescriptorSubtype != AS_GENERAL)
		return USBD_INVAL;
	DPRINTF((" streadming_endpoint: offset=%d bLength=%d\n", offs, sed->bLength));
	offs += sed->bLength;
	if (offs > size)
		return USBD_INVAL;

	if (sync && id->bNumEndpoints <= 1) {
		kprintf("%s: a sync-pipe endpoint but no other endpoint\n",
		       device_get_nameunit(sc->sc_dev));
		return USBD_INVAL;
	}
	if (!sync && id->bNumEndpoints > 1) {
		kprintf("%s: non sync-pipe endpoint but multiple endpoints\n",
		       device_get_nameunit(sc->sc_dev));
		return USBD_INVAL;
	}
	epdesc1 = NULL;
	if (id->bNumEndpoints > 1) {
		epdesc1 = (const void*)(buf + offs);
		if (epdesc1->bDescriptorType != UDESC_ENDPOINT)
			return USBD_INVAL;
		DPRINTF(("uaudio_process_as: endpoint[1] bLength=%d "
			 "bDescriptorType=%d bEndpointAddress=%d "
			 "bmAttributes=0x%x wMaxPacketSize=%d bInterval=%d "
			 "bRefresh=%d bSynchAddress=%d\n",
			 epdesc1->bLength, epdesc1->bDescriptorType,
			 epdesc1->bEndpointAddress, epdesc1->bmAttributes,
			 UGETW(epdesc1->wMaxPacketSize), epdesc1->bInterval,
			 epdesc1->bRefresh, epdesc1->bSynchAddress));
		offs += epdesc1->bLength;
		if (offs > size)
			return USBD_INVAL;
		if (epdesc1->bSynchAddress != 0) {
			kprintf("%s: invalid endpoint: bSynchAddress=0\n",
			       device_get_nameunit(sc->sc_dev));
			return USBD_INVAL;
		}
		if (UE_GET_XFERTYPE(epdesc1->bmAttributes) != UE_ISOCHRONOUS) {
			kprintf("%s: invalid endpoint: bmAttributes=0x%x\n",
			       device_get_nameunit(sc->sc_dev), epdesc1->bmAttributes);
			return USBD_INVAL;
		}
		if (epdesc1->bEndpointAddress != ed->bSynchAddress) {
			kprintf("%s: invalid endpoint addresses: "
			       "ep[0]->bSynchAddress=0x%x "
			       "ep[1]->bEndpointAddress=0x%x\n",
			       device_get_nameunit(sc->sc_dev), ed->bSynchAddress,
			       epdesc1->bEndpointAddress);
			return USBD_INVAL;
		}
		/* UE_GET_ADDR(epdesc1->bEndpointAddress), and epdesc1->bRefresh */
	}

	format = UGETW(asid->wFormatTag);
	chan = asf1d->bNrChannels;
	prec = asf1d->bBitResolution;
	if (prec != 8 && prec != 16 && prec != 24 && prec != 32) {
		kprintf("%s: ignored setting with precision %d\n",
		       device_get_nameunit(sc->sc_dev), prec);
		return USBD_NORMAL_COMPLETION;
	}
	switch (format) {
	case UA_FMT_PCM:
		if (prec == 8) {
			sc->sc_altflags |= HAS_8;
		} else if (prec == 16) {
			sc->sc_altflags |= HAS_16;
		} else if (prec == 24) {
			sc->sc_altflags |= HAS_24;
		} else if (prec == 32) {
			sc->sc_altflags |= HAS_32;
		}
		enc = AUDIO_ENCODING_SLINEAR_LE;
		format_str = "pcm";
		break;
	case UA_FMT_PCM8:
		enc = AUDIO_ENCODING_ULINEAR_LE;
		sc->sc_altflags |= HAS_8U;
		format_str = "pcm8";
		break;
	case UA_FMT_ALAW:
		enc = AUDIO_ENCODING_ALAW;
		sc->sc_altflags |= HAS_ALAW;
		format_str = "alaw";
		break;
	case UA_FMT_MULAW:
		enc = AUDIO_ENCODING_ULAW;
		sc->sc_altflags |= HAS_MULAW;
		format_str = "mulaw";
		break;
	case UA_FMT_IEEE_FLOAT:
	default:
		kprintf("%s: ignored setting with format %d\n",
		       device_get_nameunit(sc->sc_dev), format);
		return USBD_NORMAL_COMPLETION;
	}
#ifdef USB_DEBUG
	kprintf("%s: %s: %dch, %d/%dbit, %s,", device_get_nameunit(sc->sc_dev),
	       dir == UE_DIR_IN ? "recording" : "playback",
	       chan, prec, asf1d->bSubFrameSize * 8, format_str);
	if (asf1d->bSamFreqType == UA_SAMP_CONTNUOUS) {
		kprintf(" %d-%dHz\n", UA_SAMP_LO(asf1d), UA_SAMP_HI(asf1d));
	} else {
		int r;
		kprintf(" %d", UA_GETSAMP(asf1d, 0));
		for (r = 1; r < asf1d->bSamFreqType; r++)
			kprintf(",%d", UA_GETSAMP(asf1d, r));
		kprintf("Hz\n");
	}
#endif
	if (sc->uaudio_sndstat_flag != 0) {
		sbuf_printf(&(sc->uaudio_sndstat), "\n\t");
		sbuf_printf(&(sc->uaudio_sndstat), 
		    "mode %d:(%s) %dch, %d/%dbit, %s,", 
		    id->bAlternateSetting,
		    dir == UE_DIR_IN ? "input" : "output",
		    chan, prec, asf1d->bSubFrameSize * 8, format_str);
		if (asf1d->bSamFreqType == UA_SAMP_CONTNUOUS) {
			sbuf_printf(&(sc->uaudio_sndstat), " %d-%dHz", 
			    UA_SAMP_LO(asf1d), UA_SAMP_HI(asf1d));
		} else {
			int r;
			sbuf_printf(&(sc->uaudio_sndstat), 
			    " %d", UA_GETSAMP(asf1d, 0));
			for (r = 1; r < asf1d->bSamFreqType; r++)
				sbuf_printf(&(sc->uaudio_sndstat), 
				    ",%d", UA_GETSAMP(asf1d, r));
			sbuf_printf(&(sc->uaudio_sndstat), "Hz");
		}
	}

	ai.alt = id->bAlternateSetting;
	ai.encoding = enc;
	ai.attributes = sed->bmAttributes;
	ai.idesc = id;
	ai.edesc = ed;
	ai.edesc1 = epdesc1;
	ai.asf1desc = asf1d;
	ai.sc_busy = 0;
	ai.ifaceh = NULL;
	uaudio_add_alt(sc, &ai);
#ifdef USB_DEBUG
	if (ai.attributes & UA_SED_FREQ_CONTROL)
		DPRINTFN(1, ("uaudio_process_as:  FREQ_CONTROL\n"));
	if (ai.attributes & UA_SED_PITCH_CONTROL)
		DPRINTFN(1, ("uaudio_process_as:  PITCH_CONTROL\n"));
#endif
	sc->sc_mode |= (dir == UE_DIR_OUT) ? AUMODE_PLAY : AUMODE_RECORD;

	return USBD_NORMAL_COMPLETION;
}
#undef offs

static usbd_status
uaudio_identify_as(struct uaudio_softc *sc,
		   const usb_config_descriptor_t *cdesc)
{
	const usb_interface_descriptor_t *id;
	const char *buf;
	int size, offs;

	size = UGETW(cdesc->wTotalLength);
	buf = (const char *)cdesc;

	/* Locate the AudioStreaming interface descriptor. */
	offs = 0;
	id = uaudio_find_iface(buf, size, &offs, UISUBCLASS_AUDIOSTREAM);
	if (id == NULL)
		return USBD_INVAL;

	sc->uaudio_sndstat_flag = 0;
	if (sbuf_new(&(sc->uaudio_sndstat), NULL, 4096, SBUF_AUTOEXTEND) != NULL)
		sc->uaudio_sndstat_flag = 1;

	/* Loop through all the alternate settings. */
	while (offs <= size) {
		DPRINTFN(2, ("uaudio_identify: interface=%d offset=%d\n",
		    id->bInterfaceNumber, offs));
		switch (id->bNumEndpoints) {
		case 0:
			DPRINTFN(2, ("uaudio_identify: AS null alt=%d\n",
				     id->bAlternateSetting));
			sc->sc_nullalt = id->bAlternateSetting;
			break;
		case 1:
#ifdef UAUDIO_MULTIPLE_ENDPOINTS
		case 2:
#endif
			uaudio_process_as(sc, buf, &offs, size, id);
			break;
		default:
			kprintf("%s: ignored audio interface with %d "
			       "endpoints\n",
			       device_get_nameunit(sc->sc_dev), id->bNumEndpoints);
			break;
		}
		id = uaudio_find_iface(buf, size, &offs,UISUBCLASS_AUDIOSTREAM);
		if (id == NULL)
			break;
	}

	sbuf_finish(&(sc->uaudio_sndstat));

	if (offs > size)
		return USBD_INVAL;
	DPRINTF(("uaudio_identify_as: %d alts available\n", sc->sc_nalts));

	if (sc->sc_mode == 0) {
		kprintf("%s: no usable endpoint found\n",
		       device_get_nameunit(sc->sc_dev));
		return USBD_INVAL;
	}

	return USBD_NORMAL_COMPLETION;
}

static usbd_status
uaudio_identify_ac(struct uaudio_softc *sc, const usb_config_descriptor_t *cdesc)
{
	struct io_terminal* iot;
	const usb_interface_descriptor_t *id;
	const struct usb_audio_control_descriptor *acdp;
	const usb_descriptor_t *dp;
	const struct usb_audio_output_terminal *pot;
	struct terminal_list *tml;
	const char *buf, *ibuf, *ibufend;
	int size, offs, aclen, ndps, i, j;

	size = UGETW(cdesc->wTotalLength);
	buf = (const char *)cdesc;

	/* Locate the AudioControl interface descriptor. */
	offs = 0;
	id = uaudio_find_iface(buf, size, &offs, UISUBCLASS_AUDIOCONTROL);
	if (id == NULL)
		return USBD_INVAL;
	if (offs + sizeof *acdp > size)
		return USBD_INVAL;
	sc->sc_ac_iface = id->bInterfaceNumber;
	DPRINTFN(2,("uaudio_identify_ac: AC interface is %d\n", sc->sc_ac_iface));

	/* A class-specific AC interface header should follow. */
	ibuf = buf + offs;
	acdp = (const struct usb_audio_control_descriptor *)ibuf;
	if (acdp->bDescriptorType != UDESC_CS_INTERFACE ||
	    acdp->bDescriptorSubtype != UDESCSUB_AC_HEADER)
		return USBD_INVAL;
	aclen = UGETW(acdp->wTotalLength);
	if (offs + aclen > size)
		return USBD_INVAL;

	if (!(usbd_get_quirks(sc->sc_udev)->uq_flags & UQ_BAD_ADC) &&
	     UGETW(acdp->bcdADC) != UAUDIO_VERSION)
		return USBD_INVAL;

	sc->sc_audio_rev = UGETW(acdp->bcdADC);
	DPRINTFN(2,("uaudio_identify_ac: found AC header, vers=%03x, len=%d\n",
		 sc->sc_audio_rev, aclen));

	sc->sc_nullalt = -1;

	/* Scan through all the AC specific descriptors */
	ibufend = ibuf + aclen;
	dp = (const usb_descriptor_t *)ibuf;
	ndps = 0;
	iot = kmalloc(sizeof(struct io_terminal) * 256,
		      M_TEMP, M_WAITOK | M_ZERO);
	for (;;) {
		ibuf += dp->bLength;
		if (ibuf >= ibufend)
			break;
		dp = (const usb_descriptor_t *)ibuf;
		if (ibuf + dp->bLength > ibufend) {
			kfree(iot, M_TEMP);
			return USBD_INVAL;
		}
		if (dp->bDescriptorType != UDESC_CS_INTERFACE) {
			kprintf("uaudio_identify_ac: skip desc type=0x%02x\n",
			       dp->bDescriptorType);
			continue;
		}
		i = ((const struct usb_audio_input_terminal *)dp)->bTerminalId;
		iot[i].d.desc = dp;
		if (i > ndps)
			ndps = i;
	}
	ndps++;

	/* construct io_terminal */
	for (i = 0; i < ndps; i++) {
		dp = iot[i].d.desc;
		if (dp == NULL)
			continue;
		if (dp->bDescriptorSubtype != UDESCSUB_AC_OUTPUT)
			continue;
		pot = iot[i].d.ot;
		tml = uaudio_io_terminaltype(UGETW(pot->wTerminalType), iot, i);
		if (tml != NULL)
			kfree(tml, M_TEMP);
	}

#ifdef USB_DEBUG
	for (i = 0; i < 256; i++) {
		struct usb_audio_cluster cluster;

		if (iot[i].d.desc == NULL)
			continue;
		kprintf("id %d:\t", i);
		switch (iot[i].d.desc->bDescriptorSubtype) {
		case UDESCSUB_AC_INPUT:
			kprintf("AC_INPUT type=%s\n", uaudio_get_terminal_name
				  (UGETW(iot[i].d.it->wTerminalType)));
			kprintf("\t");
			cluster = uaudio_get_cluster(i, iot);
			uaudio_dump_cluster(&cluster);
			kprintf("\n");
			break;
		case UDESCSUB_AC_OUTPUT:
			kprintf("AC_OUTPUT type=%s ", uaudio_get_terminal_name
				  (UGETW(iot[i].d.ot->wTerminalType)));
			kprintf("src=%d\n", iot[i].d.ot->bSourceId);
			break;
		case UDESCSUB_AC_MIXER:
			kprintf("AC_MIXER src=");
			for (j = 0; j < iot[i].d.mu->bNrInPins; j++)
				kprintf("%d ", iot[i].d.mu->baSourceId[j]);
			kprintf("\n\t");
			cluster = uaudio_get_cluster(i, iot);
			uaudio_dump_cluster(&cluster);
			kprintf("\n");
			break;
		case UDESCSUB_AC_SELECTOR:
			kprintf("AC_SELECTOR src=");
			for (j = 0; j < iot[i].d.su->bNrInPins; j++)
				kprintf("%d ", iot[i].d.su->baSourceId[j]);
			kprintf("\n");
			break;
		case UDESCSUB_AC_FEATURE:
			kprintf("AC_FEATURE src=%d\n", iot[i].d.fu->bSourceId);
			break;
		case UDESCSUB_AC_PROCESSING:
			kprintf("AC_PROCESSING src=");
			for (j = 0; j < iot[i].d.pu->bNrInPins; j++)
				kprintf("%d ", iot[i].d.pu->baSourceId[j]);
			kprintf("\n\t");
			cluster = uaudio_get_cluster(i, iot);
			uaudio_dump_cluster(&cluster);
			kprintf("\n");
			break;
		case UDESCSUB_AC_EXTENSION:
			kprintf("AC_EXTENSION src=");
			for (j = 0; j < iot[i].d.eu->bNrInPins; j++)
				kprintf("%d ", iot[i].d.eu->baSourceId[j]);
			kprintf("\n\t");
			cluster = uaudio_get_cluster(i, iot);
			uaudio_dump_cluster(&cluster);
			kprintf("\n");
			break;
		default:
			kprintf("unknown audio control (subtype=%d)\n",
				  iot[i].d.desc->bDescriptorSubtype);
		}
		for (j = 0; j < iot[i].inputs_size; j++) {
			int k;
			kprintf("\tinput%d: ", j);
			tml = iot[i].inputs[j];
			if (tml == NULL) {
				kprintf("NULL\n");
				continue;
			}
			for (k = 0; k < tml->size; k++)
				kprintf("%s ", uaudio_get_terminal_name
					  (tml->terminals[k]));
			kprintf("\n");
		}
		kprintf("\toutput: ");
		tml = iot[i].output;
		for (j = 0; j < tml->size; j++)
			kprintf("%s ", uaudio_get_terminal_name(tml->terminals[j]));
		kprintf("\n");
	}
#endif

	for (i = 0; i < ndps; i++) {
		dp = iot[i].d.desc;
		if (dp == NULL)
			continue;
		DPRINTF(("uaudio_identify_ac: id=%d subtype=%d\n",
			 i, dp->bDescriptorSubtype));
		switch (dp->bDescriptorSubtype) {
		case UDESCSUB_AC_HEADER:
			kprintf("uaudio_identify_ac: unexpected AC header\n");
			break;
		case UDESCSUB_AC_INPUT:
			uaudio_add_input(sc, iot, i);
			break;
		case UDESCSUB_AC_OUTPUT:
			uaudio_add_output(sc, iot, i);
			break;
		case UDESCSUB_AC_MIXER:
			uaudio_add_mixer(sc, iot, i);
			break;
		case UDESCSUB_AC_SELECTOR:
			uaudio_add_selector(sc, iot, i);
			break;
		case UDESCSUB_AC_FEATURE:
			uaudio_add_feature(sc, iot, i);
			break;
		case UDESCSUB_AC_PROCESSING:
			uaudio_add_processing(sc, iot, i);
			break;
		case UDESCSUB_AC_EXTENSION:
			uaudio_add_extension(sc, iot, i);
			break;
		default:
			kprintf("uaudio_identify_ac: bad AC desc subtype=0x%02x\n",
			       dp->bDescriptorSubtype);
			break;
		}
	}

	/* delete io_terminal */
	for (i = 0; i < 256; i++) {
		if (iot[i].d.desc == NULL)
			continue;
		if (iot[i].inputs != NULL) {
			for (j = 0; j < iot[i].inputs_size; j++) {
				if (iot[i].inputs[j] != NULL)
					kfree(iot[i].inputs[j], M_TEMP);
			}
			kfree(iot[i].inputs, M_TEMP);
		}
		if (iot[i].output != NULL)
			kfree(iot[i].output, M_TEMP);
		iot[i].d.desc = NULL;
	}
	kfree(iot, M_TEMP);

	return USBD_NORMAL_COMPLETION;
}

static int
uaudio_get(struct uaudio_softc *sc, int which, int type, int wValue,
	   int wIndex, int len)
{
	usb_device_request_t req;
	uint8_t data[4];
	usbd_status err;
	int val;

	if (sc->sc_dying)
		return EIO;

	if (wValue == -1)
		return 0;

	req.bmRequestType = type;
	req.bRequest = which;
	USETW(req.wValue, wValue);
	USETW(req.wIndex, wIndex);
	USETW(req.wLength, len);
	DPRINTFN(2,("uaudio_get: type=0x%02x req=0x%02x wValue=0x%04x "
		    "wIndex=0x%04x len=%d\n",
		    type, which, wValue, wIndex, len));
	err = usbd_do_request(sc->sc_udev, &req, data);
	if (err) {
		DPRINTF(("uaudio_get: err=%s\n", usbd_errstr(err)));
		return -1;
	}
	switch (len) {
	case 1:
		val = data[0];
		break;
	case 2:
		val = data[0] | (data[1] << 8);
		break;
	default:
		DPRINTF(("uaudio_get: bad length=%d\n", len));
		return -1;
	}
	DPRINTFN(2,("uaudio_get: val=%d\n", val));
	return val;
}

static void
uaudio_set(struct uaudio_softc *sc, int which, int type, int wValue,
	   int wIndex, int len, int val)
{
	usb_device_request_t req;
	uint8_t data[4];
	usbd_status err;

	if (sc->sc_dying)
		return;

	if (wValue == -1)
		return;

	req.bmRequestType = type;
	req.bRequest = which;
	USETW(req.wValue, wValue);
	USETW(req.wIndex, wIndex);
	USETW(req.wLength, len);
	switch (len) {
	case 1:
		data[0] = val;
		break;
	case 2:
		data[0] = val;
		data[1] = val >> 8;
		break;
	default:
		return;
	}
	DPRINTFN(2,("uaudio_set: type=0x%02x req=0x%02x wValue=0x%04x "
		    "wIndex=0x%04x len=%d, val=%d\n",
		    type, which, wValue, wIndex, len, val & 0xffff));
	err = usbd_do_request(sc->sc_udev, &req, data);
#ifdef USB_DEBUG
	if (err)
		DPRINTF(("uaudio_set: err=%d\n", err));
#endif
}

static int
uaudio_signext(int type, int val)
{
	if (!MIX_UNSIGNED(type)) {
		if (MIX_SIZE(type) == 2)
			val = (int16_t)val;
		else
			val = (int8_t)val;
	}
	return val;
}

int
uaudio_bsd2value(struct mixerctl *mc, int val)
{
	DPRINTFN(5,("uaudio_bsd2value: type=%03x val=%d min=%d max=%d ",
		    mc->type, val, mc->minval, mc->maxval));
	if (mc->type == MIX_ON_OFF) {
		val = (val != 0);
	} else if (mc->type == MIX_SELECTOR) {
		if (val < mc->minval || val > mc->maxval)
			val = mc->minval;
	} else
		val = (val + mc->delta/2) * mc->mul / 255 + mc->minval;
	DPRINTFN(5, ("val'=%d\n", val));
	return val;
}

static void
uaudio_ctl_set(struct uaudio_softc *sc, int which, struct mixerctl *mc,
	       int chan, int val)
{
	val = uaudio_bsd2value(mc, val);
	uaudio_set(sc, which, UT_WRITE_CLASS_INTERFACE, mc->wValue[chan],
		   mc->wIndex, MIX_SIZE(mc->type), val);
}

/* Set up a pipe for a channel. */
static usbd_status
uaudio_chan_open(struct uaudio_softc *sc, struct chan *ch)
{
	struct as_info *as;
	int endpt;
	usbd_status err;

	if (sc->sc_dying)
		return EIO;

	as = &sc->sc_alts[ch->altidx];
	endpt = as->edesc->bEndpointAddress;
	DPRINTF(("uaudio_chan_open: endpt=0x%02x, speed=%d, alt=%d\n",
		 endpt, ch->sample_rate, as->alt));

	/* Set alternate interface corresponding to the mode. */
	err = usbd_set_interface(as->ifaceh, as->alt);
	if (err)
		return err;

	/*
	 * If just one sampling rate is supported,
	 * no need to call uaudio_set_speed().
	 * Roland SD-90 freezes by a SAMPLING_FREQ_CONTROL request.
	 */
	if (as->asf1desc->bSamFreqType != 1) {
		err = uaudio_set_speed(sc, endpt, ch->sample_rate);
		if (err)
			DPRINTF(("uaudio_chan_open: set_speed failed err=%s\n",
				 usbd_errstr(err)));
	}

	ch->pipe = 0;
	ch->sync_pipe = 0;
	DPRINTF(("uaudio_chan_open: create pipe to 0x%02x\n", endpt));
	err = usbd_open_pipe(as->ifaceh, endpt, 0, &ch->pipe);
	if (err)
		return err;
	if (as->edesc1 != NULL) {
		endpt = as->edesc1->bEndpointAddress;
		DPRINTF(("uaudio_chan_open: create sync-pipe to 0x%02x\n", endpt));
		err = usbd_open_pipe(as->ifaceh, endpt, 0, &ch->sync_pipe);
	}
	return err;
}

static void
uaudio_chan_close(struct uaudio_softc *sc, struct chan *ch)
{
	struct as_info *as;

	if (sc->sc_dying)
		return ;

	as = &sc->sc_alts[ch->altidx];
	as->sc_busy = 0;
	if (sc->sc_nullalt >= 0) {
		DPRINTF(("uaudio_chan_close: set null alt=%d\n",
			 sc->sc_nullalt));
		usbd_set_interface(as->ifaceh, sc->sc_nullalt);
	}
	if (ch->pipe) {
		usbd_abort_pipe(ch->pipe);
		usbd_close_pipe(ch->pipe);
	}
	if (ch->sync_pipe) {
		usbd_abort_pipe(ch->sync_pipe);
		usbd_close_pipe(ch->sync_pipe);
	}
}

static usbd_status
uaudio_chan_alloc_buffers(struct uaudio_softc *sc, struct chan *ch)
{
	usbd_xfer_handle xfer;
	void *buf;
	int i, size;

	size = (ch->bytes_per_frame + ch->sample_size) * UAUDIO_NFRAMES;
	for (i = 0; i < UAUDIO_NCHANBUFS; i++) {
		xfer = usbd_alloc_xfer(sc->sc_udev);
		if (xfer == 0)
			goto bad;
		ch->chanbufs[i].xfer = xfer;
		buf = usbd_alloc_buffer(xfer, size);
		if (buf == NULL) {
			i++;
			goto bad;
		}
		ch->chanbufs[i].buffer = buf;
		ch->chanbufs[i].chan = ch;
	}

	return USBD_NORMAL_COMPLETION;

bad:
	while (--i >= 0)
		/* implicit buffer kfree */
		usbd_free_xfer(ch->chanbufs[i].xfer);
	return USBD_NOMEM;
}

static void
uaudio_chan_free_buffers(struct uaudio_softc *sc, struct chan *ch)
{
	int i;

	for (i = 0; i < UAUDIO_NCHANBUFS; i++)
		usbd_free_xfer(ch->chanbufs[i].xfer);
}

/* Called at splusb() */
static void
uaudio_chan_ptransfer(struct chan *ch)
{
	struct chanbuf *cb;
	int i, n, size, residue, total;

	if (ch->sc->sc_dying)
		return;

	/* Pick the next channel buffer. */
	cb = &ch->chanbufs[ch->curchanbuf];
	if (++ch->curchanbuf >= UAUDIO_NCHANBUFS)
		ch->curchanbuf = 0;

	/* Compute the size of each frame in the next transfer. */
	residue = ch->residue;
	total = 0;
	for (i = 0; i < UAUDIO_NFRAMES; i++) {
		size = ch->bytes_per_frame;
		residue += ch->fraction;
		if (residue >= USB_FRAMES_PER_SECOND) {
			if ((ch->sc->sc_altflags & UA_NOFRAC) == 0)
				size += ch->sample_size;
			residue -= USB_FRAMES_PER_SECOND;
		}
		cb->sizes[i] = size;
		total += size;
	}
	ch->residue = residue;
	cb->size = total;

	/*
	 * Transfer data from upper layer buffer to channel buffer, taking
	 * care of wrapping the upper layer buffer.
	 */
	n = min(total, ch->end - ch->cur);
	memcpy(cb->buffer, ch->cur, n);
	ch->cur += n;
	if (ch->cur >= ch->end)
		ch->cur = ch->start;
	if (total > n) {
		total -= n;
		memcpy(cb->buffer + n, ch->cur, total);
		ch->cur += total;
	}

#ifdef USB_DEBUG
	if (uaudiodebug > 8) {
		DPRINTF(("uaudio_chan_ptransfer: buffer=%p, residue=0.%03d\n",
			 cb->buffer, ch->residue));
		for (i = 0; i < UAUDIO_NFRAMES; i++) {
			DPRINTF(("   [%d] length %d\n", i, cb->sizes[i]));
		}
	}
#endif

	DPRINTFN(5,("uaudio_chan_transfer: ptransfer xfer=%p\n", cb->xfer));
	/* Fill the request */
	usbd_setup_isoc_xfer(cb->xfer, ch->pipe, cb, cb->sizes,
			     UAUDIO_NFRAMES, USBD_NO_COPY,
			     uaudio_chan_pintr);

	(void)usbd_transfer(cb->xfer);
}

static void
uaudio_chan_pintr(usbd_xfer_handle xfer, usbd_private_handle priv,
		  usbd_status status)
{
	struct chanbuf *cb;
	struct chan *ch;
	u_int32_t count;

	cb = priv;
	ch = cb->chan;
	/* Return if we are aborting. */
	if (status == USBD_CANCELLED)
		return;

	usbd_get_xfer_status(xfer, NULL, NULL, &count, NULL);
	DPRINTFN(5,("uaudio_chan_pintr: count=%d, transferred=%d\n",
		    count, ch->transferred));
#ifdef DIAGNOSTIC
	if (count != cb->size) {
		kprintf("uaudio_chan_pintr: count(%d) != size(%d)\n",
		       count, cb->size);
	}
#endif

	ch->transferred += cb->size;

	crit_enter();
	chn_intr(ch->pcm_ch);
	crit_exit();

	/* start next transfer */
	uaudio_chan_ptransfer(ch);
}

/* Called at splusb() */
static void
uaudio_chan_rtransfer(struct chan *ch)
{
	struct chanbuf *cb;
	int i, size, residue, total;

	if (ch->sc->sc_dying)
		return;

	/* Pick the next channel buffer. */
	cb = &ch->chanbufs[ch->curchanbuf];
	if (++ch->curchanbuf >= UAUDIO_NCHANBUFS)
		ch->curchanbuf = 0;

	/* Compute the size of each frame in the next transfer. */
	residue = ch->residue;
	total = 0;
	for (i = 0; i < UAUDIO_NFRAMES; i++) {
		size = ch->bytes_per_frame;
		cb->sizes[i] = size;
		cb->offsets[i] = total;
		total += size;
	}
	ch->residue = residue;
	cb->size = total;

#ifdef USB_DEBUG
	if (uaudiodebug > 8) {
		DPRINTF(("uaudio_chan_rtransfer: buffer=%p, residue=0.%03d\n",
			 cb->buffer, ch->residue));
		for (i = 0; i < UAUDIO_NFRAMES; i++) {
			DPRINTF(("   [%d] length %d\n", i, cb->sizes[i]));
		}
	}
#endif

	DPRINTFN(5,("uaudio_chan_rtransfer: transfer xfer=%p\n", cb->xfer));
	/* Fill the request */
	usbd_setup_isoc_xfer(cb->xfer, ch->pipe, cb, cb->sizes,
			     UAUDIO_NFRAMES, USBD_NO_COPY,
			     uaudio_chan_rintr);

	(void)usbd_transfer(cb->xfer);
}

static void
uaudio_chan_rintr(usbd_xfer_handle xfer, usbd_private_handle priv,
		  usbd_status status)
{
	struct chanbuf *cb = priv;
	struct chan *ch = cb->chan;
	u_int32_t count;
	int i, n, frsize;

	/* Return if we are aborting. */
	if (status == USBD_CANCELLED)
		return;

	usbd_get_xfer_status(xfer, NULL, NULL, &count, NULL);
	DPRINTFN(5,("uaudio_chan_rintr: count=%d, transferred=%d\n",
		    count, ch->transferred));

	/* count < cb->size is normal for asynchronous source */
#ifdef DIAGNOSTIC
	if (count > cb->size) {
		kprintf("uaudio_chan_rintr: count(%d) > size(%d)\n",
		       count, cb->size);
	}
#endif

	/*
	 * Transfer data from channel buffer to upper layer buffer, taking
	 * care of wrapping the upper layer buffer.
	 */
	for(i = 0; i < UAUDIO_NFRAMES; i++) {
		frsize = cb->sizes[i];
		n = min(frsize, ch->end - ch->cur);
		memcpy(ch->cur, cb->buffer + cb->offsets[i], n);
		ch->cur += n;
		if (ch->cur >= ch->end)
			ch->cur = ch->start;
		if (frsize > n) {
			memcpy(ch->cur, cb->buffer + cb->offsets[i] + n,
			    frsize - n);
			ch->cur += frsize - n;
		}
	}

	/* Call back to upper layer */
	ch->transferred += count;

	crit_enter();
	chn_intr(ch->pcm_ch);
	crit_exit();

	/* start next transfer */
	uaudio_chan_rtransfer(ch);
}

static usbd_status
uaudio_set_speed(struct uaudio_softc *sc, int endpt, u_int speed)
{
	usb_device_request_t req;
	uint8_t data[3];

	DPRINTFN(5,("uaudio_set_speed: endpt=%d speed=%u\n", endpt, speed));
	req.bmRequestType = UT_WRITE_CLASS_ENDPOINT;
	req.bRequest = SET_CUR;
	USETW2(req.wValue, SAMPLING_FREQ_CONTROL, 0);
	USETW(req.wIndex, endpt);
	USETW(req.wLength, 3);
	data[0] = speed;
	data[1] = speed >> 8;
	data[2] = speed >> 16;

	return usbd_do_request(sc->sc_udev, &req, data);
}


/************************************************************/
int
uaudio_init_params(struct uaudio_softc *sc, struct chan *ch, int mode)
{
	int i, j, enc;
	int samples_per_frame, sample_size;

	if ((sc->sc_playchan.pipe != NULL) || (sc->sc_recchan.pipe != NULL))
		return (-1);

	switch(ch->format & 0x000FFFFF) {
	case AFMT_U8:
		enc = AUDIO_ENCODING_ULINEAR_LE;
		ch->precision = 8;
		break;
	case AFMT_S8:
		enc = AUDIO_ENCODING_SLINEAR_LE;
		ch->precision = 8;
		break;
	case AFMT_A_LAW:	/* ? */
		enc = AUDIO_ENCODING_ALAW;
		ch->precision = 8;
		break;
	case AFMT_MU_LAW:	/* ? */
		enc = AUDIO_ENCODING_ULAW;
		ch->precision = 8;
		break;
	case AFMT_S16_LE:
		enc = AUDIO_ENCODING_SLINEAR_LE;
		ch->precision = 16;
		break;
	case AFMT_S16_BE:
		enc = AUDIO_ENCODING_SLINEAR_BE;
		ch->precision = 16;
		break;
	case AFMT_U16_LE:
		enc = AUDIO_ENCODING_ULINEAR_LE;
		ch->precision = 16;
		break;
	case AFMT_U16_BE:
		enc = AUDIO_ENCODING_ULINEAR_BE;
		ch->precision = 16;
		break;
	case AFMT_S24_LE:
		enc = AUDIO_ENCODING_SLINEAR_LE;
		ch->precision = 24;
		break;
	case AFMT_S24_BE:
		enc = AUDIO_ENCODING_SLINEAR_BE;
		ch->precision = 24;
		break;
	case AFMT_U24_LE:
		enc = AUDIO_ENCODING_ULINEAR_LE;
		ch->precision = 24;
		break;
	case AFMT_U24_BE:
		enc = AUDIO_ENCODING_ULINEAR_BE;
		ch->precision = 24;
		break;
	case AFMT_S32_LE:
		enc = AUDIO_ENCODING_SLINEAR_LE;
		ch->precision = 32;
		break;
	case AFMT_S32_BE:
		enc = AUDIO_ENCODING_SLINEAR_BE;
		ch->precision = 32;
		break;
	case AFMT_U32_LE:
		enc = AUDIO_ENCODING_ULINEAR_LE;
		ch->precision = 32;
		break;
	case AFMT_U32_BE:
		enc = AUDIO_ENCODING_ULINEAR_BE;
		ch->precision = 32;
		break;
	default:
		enc = 0;
		ch->precision = 16;
		kprintf("Unknown format %x\n", ch->format);
	}

	if (ch->format & AFMT_STEREO) {
		ch->channels = 2;
	} else {
		ch->channels = 1;
	}

/*	for (mode =  ......	 */
		for (i = 0; i < sc->sc_nalts; i++) {
			const struct usb_audio_streaming_type1_descriptor *a1d =
				sc->sc_alts[i].asf1desc;
			if (ch->channels == a1d->bNrChannels &&
			    ch->precision == a1d->bBitResolution &&
#if 0
			    enc == sc->sc_alts[i].encoding) {
#else
			    enc == sc->sc_alts[i].encoding &&
			    (mode == AUMODE_PLAY ? UE_DIR_OUT : UE_DIR_IN) ==
			    UE_GET_DIR(sc->sc_alts[i].edesc->bEndpointAddress)) {
#endif
				if (a1d->bSamFreqType == UA_SAMP_CONTNUOUS) {
					DPRINTFN(2,("uaudio_set_params: cont %d-%d\n",
					    UA_SAMP_LO(a1d), UA_SAMP_HI(a1d)));
					if (UA_SAMP_LO(a1d) <= ch->sample_rate &&
					    ch->sample_rate <= UA_SAMP_HI(a1d)) {
						if (mode == AUMODE_PLAY)
							sc->sc_playchan.altidx = i;
						else
							sc->sc_recchan.altidx = i;
						goto found;
					}
				} else {
					for (j = 0; j < a1d->bSamFreqType; j++) {
						DPRINTFN(2,("uaudio_set_params: disc #"
						    "%d: %d\n", j, UA_GETSAMP(a1d, j)));
						/* XXX allow for some slack */
						if (UA_GETSAMP(a1d, j) ==
						    ch->sample_rate) {
							if (mode == AUMODE_PLAY)
								sc->sc_playchan.altidx = i;
							else
								sc->sc_recchan.altidx = i;
							goto found;
						}
					}
				}
			}
		}
		/* return (EINVAL); */
		if (mode == AUMODE_PLAY) 
			kprintf("uaudio: This device can't play in rate=%d.\n", ch->sample_rate);
		else
			kprintf("uaudio: This device can't record in rate=%d.\n", ch->sample_rate);
		return (-1);

	found:
#if 0 /* XXX */
		p->sw_code = swcode;
		p->factor  = factor;
		if (usemode == mode)
			sc->sc_curaltidx = i;
#endif
/*	} */

	sample_size = ch->precision * ch->channels / 8;
	samples_per_frame = ch->sample_rate / USB_FRAMES_PER_SECOND;
	ch->fraction = ch->sample_rate % USB_FRAMES_PER_SECOND;
	ch->sample_size = sample_size;
	ch->bytes_per_frame = samples_per_frame * sample_size;
	ch->residue = 0;

	ch->cur = ch->start;
	ch->transferred = 0;
	ch->curchanbuf = 0;
	return (0);
}

struct uaudio_conversion {
	uint8_t uaudio_fmt;
	uint8_t uaudio_prec;
	uint32_t freebsd_fmt;
};

const struct uaudio_conversion const accepted_conversion[] = {
	{AUDIO_ENCODING_ULINEAR_LE, 8, AFMT_U8},
	{AUDIO_ENCODING_ULINEAR_LE, 16, AFMT_U16_LE},
	{AUDIO_ENCODING_ULINEAR_LE, 24, AFMT_U24_LE},
	{AUDIO_ENCODING_ULINEAR_LE, 32, AFMT_U32_LE},
	{AUDIO_ENCODING_ULINEAR_BE, 16, AFMT_U16_BE},
	{AUDIO_ENCODING_ULINEAR_BE, 24, AFMT_U24_BE},
	{AUDIO_ENCODING_ULINEAR_BE, 32, AFMT_U32_BE},
	{AUDIO_ENCODING_SLINEAR_LE, 8, AFMT_S8},
	{AUDIO_ENCODING_SLINEAR_LE, 16, AFMT_S16_LE},
	{AUDIO_ENCODING_SLINEAR_LE, 24, AFMT_S24_LE},
	{AUDIO_ENCODING_SLINEAR_LE, 32, AFMT_S32_LE},
	{AUDIO_ENCODING_SLINEAR_BE, 16, AFMT_S16_BE},
	{AUDIO_ENCODING_SLINEAR_BE, 24, AFMT_S24_BE},
	{AUDIO_ENCODING_SLINEAR_BE, 32, AFMT_S32_BE},
	{AUDIO_ENCODING_ALAW, 8, AFMT_A_LAW},
	{AUDIO_ENCODING_ULAW, 8, AFMT_MU_LAW},
	{0,0,0}
};

unsigned
uaudio_query_formats(device_t dev, int reqdir, unsigned maxfmt, struct pcmchan_caps *cap)
{
	struct uaudio_softc *sc;
	const struct usb_audio_streaming_type1_descriptor *asf1d;
	const struct uaudio_conversion *iterator;
	unsigned fmtcount, foundcount;
	u_int32_t fmt;
	uint8_t format, numchan, subframesize, prec, dir, iscontinuous;
	int freq, freq_min, freq_max;
	char *numchannel_descr;
	char freq_descr[64];
	int i,r;

	sc = device_get_softc(dev);
	if (sc == NULL)
		return 0;

	cap->minspeed = cap->maxspeed = 0;
	foundcount = fmtcount = 0;

	for (i = 0; i < sc->sc_nalts; i++) {
		dir = UE_GET_DIR(sc->sc_alts[i].edesc->bEndpointAddress);

		if ((dir == UE_DIR_OUT) != (reqdir == PCMDIR_PLAY))
			continue;

		asf1d = sc->sc_alts[i].asf1desc;
		format = sc->sc_alts[i].encoding;

		numchan = asf1d->bNrChannels;
		subframesize = asf1d->bSubFrameSize;
		prec = asf1d->bBitResolution;	/* precision */
		iscontinuous = asf1d->bSamFreqType == UA_SAMP_CONTNUOUS;

		if (iscontinuous)
			ksnprintf(freq_descr, sizeof(freq_descr), "continuous min %d max %d", UA_SAMP_LO(asf1d), UA_SAMP_HI(asf1d));
		else
			ksnprintf(freq_descr, sizeof(freq_descr), "fixed frequency (%d listed formats)", asf1d->bSamFreqType);

		if (numchan == 1)
			numchannel_descr = " (mono)";
		else if (numchan == 2)
			numchannel_descr = " (stereo)";
		else
			numchannel_descr = "";

		if (bootverbose) {
			device_printf(dev, "uaudio_query_formats: found a native %s channel%s %s %dbit %dbytes/subframe X %d channels = %d bytes per sample\n",
					(dir==UE_DIR_OUT)?"playback":"record",
					numchannel_descr, freq_descr,
					prec, subframesize, numchan, subframesize*numchan);
		}
		/*
		 * Now start rejecting the ones that don't map to FreeBSD
		 */

		if (numchan != 1 && numchan != 2)
			continue;

		for (iterator = accepted_conversion ; iterator->uaudio_fmt != 0 ; iterator++)
			if (iterator->uaudio_fmt == format && iterator->uaudio_prec == prec)
				break;

		if (iterator->uaudio_fmt == 0)
			continue;

		fmt = iterator->freebsd_fmt;

		if (numchan == 2)
			fmt |= AFMT_STEREO;

		foundcount++;

		if (fmtcount >= maxfmt)
			continue;

		cap->fmtlist[fmtcount++] = fmt;

		if (iscontinuous) {
			freq_min = UA_SAMP_LO(asf1d);
			freq_max = UA_SAMP_HI(asf1d);

			if (cap->minspeed == 0 || freq_min < cap->minspeed)
				cap->minspeed = freq_min;
			if (cap->maxspeed == 0)
				cap->maxspeed = cap->minspeed;
			if (freq_max > cap->maxspeed)
				cap->maxspeed = freq_max;
		} else {
			for (r = 0; r < asf1d->bSamFreqType; r++) {
				freq = UA_GETSAMP(asf1d, r);
				if (cap->minspeed == 0 || freq < cap->minspeed)
					cap->minspeed = freq;
				if (cap->maxspeed == 0)
					cap->maxspeed = cap->minspeed;
				if (freq > cap->maxspeed)
					cap->maxspeed = freq;
			}
		}
	}
	cap->fmtlist[fmtcount] = 0;
	return foundcount;
}

void
uaudio_chan_set_param_pcm_dma_buff(device_t dev, u_char *start, u_char *end,
		struct pcm_channel *pc, int dir)
{
	struct uaudio_softc *sc;
	struct chan *ch;

	sc = device_get_softc(dev);
#ifndef NO_RECORDING
	if (dir == PCMDIR_PLAY)
		ch = &sc->sc_playchan;
	else
		ch = &sc->sc_recchan;
#else
	ch = &sc->sc_playchan;
#endif

	ch->start = start;
	ch->end = end;

	ch->pcm_ch = pc;

	return;
}

void
uaudio_chan_set_param_blocksize(device_t dev, u_int32_t blocksize, int dir)
{
	struct uaudio_softc *sc;
	struct chan *ch;

	sc = device_get_softc(dev);
#ifndef NO_RECORDING
	if (dir == PCMDIR_PLAY)
		ch = &sc->sc_playchan;
	else
		ch = &sc->sc_recchan;
#else
	ch = &sc->sc_playchan;
#endif

	ch->blksize = blocksize;

	return;
}

int
uaudio_chan_set_param_speed(device_t dev, u_int32_t speed, int reqdir)
{
	const struct uaudio_conversion *iterator;
	struct uaudio_softc *sc;
	struct chan *ch;
	int i, r, score, hiscore, bestspeed;

	sc = device_get_softc(dev);
#ifndef NO_RECORDING
	if (reqdir == PCMDIR_PLAY)
		ch = &sc->sc_playchan;
	else
		ch = &sc->sc_recchan;
#else
	ch = &sc->sc_playchan;
#endif
	/*
	 * We are successful if we find an endpoint that matches our selected format and it
	 * supports the requested speed.
	 */
	hiscore = 0;
	bestspeed = 1;
	for (i = 0; i < sc->sc_nalts; i++) {
		int dir = UE_GET_DIR(sc->sc_alts[i].edesc->bEndpointAddress);
		int format = sc->sc_alts[i].encoding;
		const struct usb_audio_streaming_type1_descriptor *asf1d = sc->sc_alts[i].asf1desc;
		int iscontinuous = asf1d->bSamFreqType == UA_SAMP_CONTNUOUS;

		if ((dir == UE_DIR_OUT) != (reqdir == PCMDIR_PLAY))
			continue;

		for (iterator = accepted_conversion ; iterator->uaudio_fmt != 0 ; iterator++)
			if (iterator->uaudio_fmt != format || iterator->freebsd_fmt != (ch->format&0xfffffff))
				continue;
			if (iscontinuous) {
				if (speed >= UA_SAMP_LO(asf1d) && speed <= UA_SAMP_HI(asf1d)) {
					ch->sample_rate = speed;
					return speed;
				} else if (speed < UA_SAMP_LO(asf1d)) {
					score = 0xfff * speed / UA_SAMP_LO(asf1d);
					if (score > hiscore) {
						bestspeed = UA_SAMP_LO(asf1d);
						hiscore = score;
					}
				} else if (speed < UA_SAMP_HI(asf1d)) {
					score = 0xfff * UA_SAMP_HI(asf1d) / speed;
					if (score > hiscore) {
						bestspeed = UA_SAMP_HI(asf1d);
						hiscore = score;
					}
				}
				continue;
			}
			for (r = 0; r < asf1d->bSamFreqType; r++) {
				if (speed == UA_GETSAMP(asf1d, r)) {
					ch->sample_rate = speed;
					return speed;
				}
				if (speed > UA_GETSAMP(asf1d, r))
					score = 0xfff * UA_GETSAMP(asf1d, r) / speed;
				else
					score = 0xfff * speed / UA_GETSAMP(asf1d, r);
				if (score > hiscore) { 
					bestspeed = UA_GETSAMP(asf1d, r);
					hiscore = score;
				}
			}
	}
	if (bestspeed != 1) {
		ch->sample_rate = bestspeed;
		return bestspeed;
	}

	return 0;
}

int
uaudio_chan_getptr(device_t dev, int dir)
{
	struct uaudio_softc *sc;
	struct chan *ch;
	int ptr;

	sc = device_get_softc(dev);
#ifndef NO_RECORDING
	if (dir == PCMDIR_PLAY)
		ch = &sc->sc_playchan;
	else
		ch = &sc->sc_recchan;
#else
	ch = &sc->sc_playchan;
#endif

	ptr = ch->cur - ch->start;

	return ptr;
}

void
uaudio_chan_set_param_format(device_t dev, u_int32_t format, int dir)
{
	struct uaudio_softc *sc;
	struct chan *ch;

	sc = device_get_softc(dev);
#ifndef NO_RECORDING
	if (dir == PCMDIR_PLAY)
		ch = &sc->sc_playchan;
	else
		ch = &sc->sc_recchan;
#else
	ch = &sc->sc_playchan;
#endif

	ch->format = format;

	return;
}

int
uaudio_halt_out_dma(device_t dev)
{
	struct uaudio_softc *sc;

	sc = device_get_softc(dev);

	DPRINTF(("uaudio_halt_out_dma: enter\n"));
	if (sc->sc_playchan.pipe != NULL) {
		uaudio_chan_close(sc, &sc->sc_playchan);
		sc->sc_playchan.pipe = 0;
		uaudio_chan_free_buffers(sc, &sc->sc_playchan);
	}
        return (0);
}

int
uaudio_halt_in_dma(device_t dev)
{
	struct uaudio_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_dying)
		return (EIO);

	DPRINTF(("uaudio_halt_in_dma: enter\n"));
	if (sc->sc_recchan.pipe != NULL) {
		uaudio_chan_close(sc, &sc->sc_recchan);
		sc->sc_recchan.pipe = NULL;
		uaudio_chan_free_buffers(sc, &sc->sc_recchan);
/*		sc->sc_recchan.intr = NULL; */
	}
	return (0);
}

int
uaudio_trigger_input(device_t dev)
{
	struct uaudio_softc *sc;
	struct chan *ch;
	usbd_status err;
	int i;

	sc = device_get_softc(dev);
	ch = &sc->sc_recchan;

	if (sc->sc_dying)
		return (EIO);

/*	uaudio_chan_set_param(ch, start, end, blksize) */
	if (uaudio_init_params(sc, ch, AUMODE_RECORD))
		return (EIO);

	err = uaudio_chan_alloc_buffers(sc, ch);
	if (err)
		return (EIO);

	err = uaudio_chan_open(sc, ch);
	if (err) {
		uaudio_chan_free_buffers(sc, ch);
		return (EIO);
	}

/*	ch->intr = intr;
	ch->arg = arg; */

	crit_enter();
	for (i = 0; i < UAUDIO_NCHANBUFS-1; i++) /* XXX -1 shouldn't be needed */
		uaudio_chan_rtransfer(ch);
	crit_exit();

	return (0);
}

int
uaudio_trigger_output(device_t dev)
{
	struct uaudio_softc *sc;
	struct chan *ch;
	usbd_status err;
	int i;

	sc = device_get_softc(dev);
	ch = &sc->sc_playchan;

	if (sc->sc_dying)
		return (EIO);

	if (uaudio_init_params(sc, ch, AUMODE_PLAY))
		return (EIO);

	err = uaudio_chan_alloc_buffers(sc, ch);
	if (err)
		return (EIO);

	err = uaudio_chan_open(sc, ch);
	if (err) {
		uaudio_chan_free_buffers(sc, ch);
		return (EIO);
	}

	crit_enter();
	for (i = 0; i < UAUDIO_NCHANBUFS-1; i++) /* XXX */
		uaudio_chan_ptransfer(ch);
	crit_exit();

        return (0);
}

u_int32_t
uaudio_query_mix_info(device_t dev)
{
	int i;
	u_int32_t mask = 0;
	struct uaudio_softc *sc;
	struct mixerctl *mc;

	sc = device_get_softc(dev);
	for (i=0; i < sc->sc_nctls; i++) {
		mc = &sc->sc_ctls[i];
		if (mc->ctl != SOUND_MIXER_NRDEVICES) {
			/* Set device mask bits. 
			   See /usr/include/machine/soundcard.h */
			mask |= (1 << mc->ctl);
		}
	}
	return mask;
}

u_int32_t
uaudio_query_recsrc_info(device_t dev)
{
	int i, rec_selector_id;
	u_int32_t mask = 0;
	struct uaudio_softc *sc;
	struct mixerctl *mc;

	sc = device_get_softc(dev);
	rec_selector_id = -1;
	for (i=0; i < sc->sc_nctls; i++) {
		mc = &sc->sc_ctls[i];
		if (mc->ctl == SOUND_MIXER_NRDEVICES && 
		    mc->type == MIX_SELECTOR && mc->class == UAC_RECORD) {
			if (rec_selector_id == -1) {
				rec_selector_id = i;
			} else {
				kprintf("There are many selectors.  Can't recognize which selector is a record source selector.\n");
				return mask;
			}
		}
	}
	if (rec_selector_id == -1)
		return mask;
	mc = &sc->sc_ctls[rec_selector_id];
	for (i = mc->minval; i <= mc->maxval; i++) {
		if (mc->slctrtype[i - 1] == SOUND_MIXER_NRDEVICES)
			continue;
		mask |= 1 << mc->slctrtype[i - 1];
	}
	return mask;
}

void
uaudio_mixer_set(device_t dev, unsigned type, unsigned left, unsigned right)
{
	int i;
	struct uaudio_softc *sc;
	struct mixerctl *mc;

	sc = device_get_softc(dev);
	for (i=0; i < sc->sc_nctls; i++) {
		mc = &sc->sc_ctls[i];
		if (mc->ctl == type) {
			if (mc->nchan == 2) {
				/* set Right */
				uaudio_ctl_set(sc, SET_CUR, mc, 1, (int)(right*255)/100);
			}
			/* set Left or Mono */
			uaudio_ctl_set(sc, SET_CUR, mc, 0, (int)(left*255)/100);
		}
	}
	return;
}

u_int32_t
uaudio_mixer_setrecsrc(device_t dev, u_int32_t src)
{
	int i, rec_selector_id;
	struct uaudio_softc *sc;
	struct mixerctl *mc;

	sc = device_get_softc(dev);
	rec_selector_id = -1;
	for (i=0; i < sc->sc_nctls; i++) {
		mc = &sc->sc_ctls[i];
		if (mc->ctl == SOUND_MIXER_NRDEVICES && 
		    mc->type == MIX_SELECTOR && mc->class == UAC_RECORD) {
			if (rec_selector_id == -1) {
				rec_selector_id = i;
			} else {
				return src; /* Can't recognize which selector is record source selector */
			}
		}
	}
	if (rec_selector_id == -1)
		return src;
	mc = &sc->sc_ctls[rec_selector_id];
	for (i = mc->minval; i <= mc->maxval; i++) {
		if (src != (1 << mc->slctrtype[i - 1]))
			continue;
		uaudio_ctl_set(sc, SET_CUR, mc, 0, i);
		return (1 << mc->slctrtype[i - 1]);
	}
	uaudio_ctl_set(sc, SET_CUR, mc, 0, mc->minval);
	return (1 << mc->slctrtype[mc->minval - 1]);
}

static int
uaudio_sndstat_prepare_pcm(struct sbuf *s, device_t dev, int verbose)
{
    	struct snddev_info *d;
    	struct snddev_channel *sce;
	struct pcm_channel *c;
	struct pcm_feeder *f;
    	int pc, rc, vc;
	device_t pa_dev = device_get_parent(dev);
	struct uaudio_softc *sc = device_get_softc(pa_dev);

	if (verbose < 1)
		return 0;

	d = device_get_softc(dev);
	if (!d)
		return ENXIO;

	snd_mtxlock(d->lock);
	if (SLIST_EMPTY(&d->channels)) {
		sbuf_printf(s, " (mixer only)");
		snd_mtxunlock(d->lock);
		return 0;
	}
	pc = rc = vc = 0;
	SLIST_FOREACH(sce, &d->channels, link) {
		c = sce->channel;
		if (c->direction == PCMDIR_PLAY) {
			if (c->flags & CHN_F_VIRTUAL)
				vc++;
			else
				pc++;
		} else
			rc++;
	}
	sbuf_printf(s, " (%dp/%dr/%dv channels%s%s)", 
			d->playcount, d->reccount, d->vchancount,
			(d->flags & SD_F_SIMPLEX)? "" : " duplex",
#ifdef USING_DEVFS
			(device_get_unit(dev) == snd_unit)? " default" : ""
#else
			""
#endif
			);

	if (sc->uaudio_sndstat_flag != 0) {
		sbuf_cat(s, sbuf_data(&(sc->uaudio_sndstat)));
	}

	if (verbose <= 1) {
		snd_mtxunlock(d->lock);
		return 0;
	}

	SLIST_FOREACH(sce, &d->channels, link) {
		c = sce->channel;
		sbuf_printf(s, "\n\t");

		KASSERT(c->bufhard != NULL && c->bufsoft != NULL,
			("hosed pcm channel setup"));

		/* it would be better to indent child channels */
		sbuf_printf(s, "%s[%s]: ", c->parentchannel? c->parentchannel->name : "", c->name);
		sbuf_printf(s, "spd %d", c->speed);
		if (c->speed != sndbuf_getspd(c->bufhard))
			sbuf_printf(s, "/%d", sndbuf_getspd(c->bufhard));
		sbuf_printf(s, ", fmt 0x%08x", c->format);
		if (c->format != sndbuf_getfmt(c->bufhard))
			sbuf_printf(s, "/0x%08x", sndbuf_getfmt(c->bufhard));
		sbuf_printf(s, ", flags 0x%08x, 0x%08x", c->flags, c->feederflags);
		if (c->pid != -1)
			sbuf_printf(s, ", pid %d", c->pid);
		sbuf_printf(s, "\n\t");

		sbuf_printf(s, "interrupts %d, ", c->interrupts);
		if (c->direction == PCMDIR_REC)
			sbuf_printf(s, "overruns %d, hfree %d, sfree %d",
				c->xruns, sndbuf_getfree(c->bufhard), sndbuf_getfree(c->bufsoft));
		else
			sbuf_printf(s, "underruns %d, ready %d",
				c->xruns, sndbuf_getready(c->bufsoft));
		sbuf_printf(s, "\n\t");

		sbuf_printf(s, "{%s}", (c->direction == PCMDIR_REC)? "hardware" : "userland");
		sbuf_printf(s, " -> ");
		f = c->feeder;
		while (f->source != NULL)
			f = f->source;
		while (f != NULL) {
			sbuf_printf(s, "%s", f->class->name);
			if (f->desc->type == FEEDER_FMT)
				sbuf_printf(s, "(0x%08x -> 0x%08x)", f->desc->in, f->desc->out);
			if (f->desc->type == FEEDER_RATE)
				sbuf_printf(s, "(%d -> %d)", FEEDER_GET(f, FEEDRATE_SRC), FEEDER_GET(f, FEEDRATE_DST));
			if (f->desc->type == FEEDER_ROOT || f->desc->type == FEEDER_MIXER)
				sbuf_printf(s, "(0x%08x)", f->desc->out);
			sbuf_printf(s, " -> ");
			f = f->parent;
		}
		sbuf_printf(s, "{%s}", (c->direction == PCMDIR_REC)? "userland" : "hardware");
	}
	snd_mtxunlock(d->lock);

	return 0;
}

void
uaudio_sndstat_register(device_t dev)
{
	struct snddev_info *d = device_get_softc(dev);
	sndstat_register(dev, d->status, uaudio_sndstat_prepare_pcm);
}
	
static int
audio_attach_mi(device_t dev)
{
	device_t child;
	struct sndcard_func *func;

	/* Attach the children. */
	/* PCM Audio */
	func = kmalloc(sizeof(struct sndcard_func), M_DEVBUF, M_WAITOK | M_ZERO);
	func->func = SCF_PCM;
	child = device_add_child(dev, "pcm", -1);
	device_set_ivars(child, func);

	bus_generic_attach(dev);

	return 0; /* XXXXX */
}

DRIVER_MODULE(uaudio, uhub, uaudio_driver, uaudio_devclass, usbd_driver_load, NULL);
MODULE_VERSION(uaudio, 1);
