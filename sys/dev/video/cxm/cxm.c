/*
 * Copyright (c) 2003, 2004, 2005
 *	John Wehle <john@feith.com>.  All rights reserved.
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
 *	This product includes software developed by John Wehle.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Conexant MPEG-2 Codec driver. Supports the CX23415 / CX23416
 * chips that are on the Hauppauge PVR-250 and PVR-350 video
 * capture cards.  Currently only the encoder is supported.
 *
 * This driver was written using the invaluable information
 * compiled by The IvyTV Project (ivtv.sourceforge.net).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/mman.h>
#include <sys/module.h>
#include <sys/event.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/thread2.h>
#include <sys/vnode.h>
#include <sys/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/clock.h>

#include <dev/video/meteor/ioctl_meteor.h>
#include <dev/video/bktr/ioctl_bt848.h>

#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>

#include <dev/video/cxm/cxm.h>

#include <bus/iicbus/iiconf.h>

/*
 * Various supported device vendors/types and their names.
 */
static struct cxm_dev cxm_devs[] = {
	{ PCI_VENDOR_ICOMPRESSION, PCI_PRODUCT_ICOMPRESSION_ITVC15,
		"Conexant iTVC15 MPEG Coder" },
	{ PCI_VENDOR_ICOMPRESSION, PCI_PRODUCT_ICOMPRESSION_ITVC16,
		"Conexant iTVC16 MPEG Coder" },
	{ 0, 0, NULL }
};


static int	cxm_probe(device_t dev);
static int	cxm_attach(device_t dev);
static int	cxm_detach(device_t dev);
static int	cxm_shutdown(device_t dev);
static void	cxm_intr(void *arg);

static void	cxm_child_detached(device_t dev, device_t child);
static int	cxm_read_ivar(device_t bus, device_t dev,
			       int index, uintptr_t* val);
static int	cxm_write_ivar(device_t bus, device_t dev,
				int index, uintptr_t val);


static device_method_t cxm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         cxm_probe),
	DEVMETHOD(device_attach,        cxm_attach),
	DEVMETHOD(device_detach,        cxm_detach),
	DEVMETHOD(device_shutdown,      cxm_shutdown),

	/* bus interface */
	DEVMETHOD(bus_child_detached,   cxm_child_detached),
	DEVMETHOD(bus_print_child,      bus_generic_print_child),
	DEVMETHOD(bus_driver_added,     bus_generic_driver_added),
	DEVMETHOD(bus_read_ivar,        cxm_read_ivar),
	DEVMETHOD(bus_write_ivar,       cxm_write_ivar),

	DEVMETHOD_END
};

static driver_t cxm_driver = {
	"cxm",
	cxm_methods,
	sizeof(struct cxm_softc),
};

static devclass_t cxm_devclass;

static	d_open_t	cxm_open;
static	d_close_t	cxm_close;
static	d_read_t	cxm_read;
static	d_ioctl_t	cxm_ioctl;
static	d_kqfilter_t	cxm_kqfilter;

static void cxm_filter_detach(struct knote *);
static int cxm_filter(struct knote *, long);

static struct dev_ops cxm_ops = {
	{ "cxm", 0, 0 },
	.d_open =	cxm_open,
	.d_close =	cxm_close,
	.d_read =	cxm_read,
	.d_ioctl =	cxm_ioctl,
	.d_kqfilter =	cxm_kqfilter
};

MODULE_DEPEND(cxm, cxm_iic, 1, 1, 1);
DRIVER_MODULE(cxm, pci, cxm_driver, cxm_devclass, NULL, NULL);


static struct cxm_codec_audio_format codec_audio_formats[] = {
	{ 44100, 0xb8 }, /* 44.1 Khz, MPEG-1 Layer II, 224 kb/s */
	{ 48000, 0xe9 }  /* 48 Khz, MPEG-1 Layer II, 384 kb/s */
};


/*
 * Various profiles.
 */
static struct cxm_codec_profile vcd_ntsc_profile = {
	"MPEG-1 VideoCD NTSC video and MPEG audio",
	CXM_FW_STREAM_TYPE_VCD,
	30,
	352, 240, 480,
	{ 10, 12, 21 },
	12,
	0,
	{ 1, 1150000, 0 },
	{ 1, 15, 3},
	/*
	 * Spatial filter = Manual, Temporal filter = Manual
	 * Median filter = Horizontal / Vertical
	 * Spatial filter value = 1, Temporal filter value = 4
	 */
	{ 0, 3, 1, 4 },
	44100
};

static struct cxm_codec_profile vcd_pal_profile = {
	"MPEG-1 VideoCD PAL video and MPEG audio",
	CXM_FW_STREAM_TYPE_VCD,
	25,
	352, 288, 576,
	{ 6, 17, 22 },
	8,
	0,
	{ 1, 1150000, 0 },
	{ 1, 12, 3},
	/*
	 * Spatial filter = Manual, Temporal filter = Manual
	 * Median filter = Horizontal / Vertical
	 * Spatial filter value = 1, Temporal filter value = 4
	 */
	{ 0, 3, 1, 4 },
	44100
};

static struct cxm_codec_profile svcd_ntsc_profile = {
	"MPEG-2 SuperVCD NTSC video and MPEG audio",
	CXM_FW_STREAM_TYPE_SVCD,
	30,
	480, 480, 480,
	{ 10, 12, 21 },
	2,
	0,
	/* 2.5 Mb/s peak limit to keep bbdmux followed by mplex -f 4 happy */
	{ 0, 1150000, 2500000 },
	{ 1, 15, 3},
	/*
	 * Spatial filter = Manual, Temporal filter = Manual
	 * Median filter = Horizontal / Vertical
	 * Spatial filter value = 1, Temporal filter value = 4
	 */
	{ 0, 3, 1, 4 },
	44100
};

static struct cxm_codec_profile svcd_pal_profile = {
	"MPEG-2 SuperVCD PAL video and MPEG audio",
	CXM_FW_STREAM_TYPE_SVCD,
	25,
	480, 576, 576,
	{ 6, 17, 22 },
	2,
	0,
	/* 2.5 Mb/s peak limit to keep bbdmux followed by mplex -f 4 happy */
	{ 0, 1150000, 2500000 },
	{ 1, 12, 3},
	/*
	 * Spatial filter = Manual, Temporal filter = Manual
	 * Median filter = Horizontal / Vertical
	 * Spatial filter value = 1, Temporal filter value = 4
	 */
	{ 0, 3, 1, 4 },
	44100
};

static struct cxm_codec_profile dvd_half_d1_ntsc_profile = {
	"MPEG-2 DVD NTSC video and MPEG audio",
	CXM_FW_STREAM_TYPE_DVD,
	30,
	352, 480, 480,
	{ 10, 12, 21 },
	2,
	0,
	{ 0, 4000000, 4520000 }, /* 4 hours on 8.54 GB media */
	{ 1, 15, 3},
	/*
	 * Spatial filter = Manual, Temporal filter = Manual
	 * Median filter = Horizontal / Vertical
	 * Spatial filter value = 1, Temporal filter value = 4
	 */
	{ 0, 3, 1, 4 },
	48000
};

static struct cxm_codec_profile dvd_half_d1_pal_profile = {
	"MPEG-2 DVD PAL video and MPEG audio",
	CXM_FW_STREAM_TYPE_DVD,
	25,
	352, 576, 576,
	{ 6, 17, 22 },
	2,
	0,
	{ 0, 4000000, 4520000 }, /* 4 hours on 8.54 GB media */
	{ 1, 12, 3},
	/*
	 * Spatial filter = Manual, Temporal filter = Manual
	 * Median filter = Horizontal / Vertical
	 * Spatial filter value = 1, Temporal filter value = 4
	 */
	{ 0, 3, 1, 4 },
	48000
};

static struct cxm_codec_profile dvd_full_d1_ntsc_profile = {
	"MPEG-2 DVD NTSC video and MPEG audio",
	CXM_FW_STREAM_TYPE_DVD,
	30,
	720, 480, 480,
	{ 10, 12, 21 },
	2,
	0,
	/* 9.52 Mb/s peak limit to keep bbdmux followed by mplex -f 8 happy */
	{ 0, 9000000, 9520000 }, /* 1 hour on 4.7 GB media */
	{ 1, 15, 3},
	/*
	 * Spatial filter = Manual, Temporal filter = Manual
	 * Median filter = Horizontal / Vertical
	 * Spatial filter value = 1, Temporal filter value = 4
	 */
	{ 0, 3, 1, 4 },
	48000
};

static struct cxm_codec_profile dvd_full_d1_pal_profile = {
	"MPEG-2 DVD PAL video and MPEG audio",
	CXM_FW_STREAM_TYPE_DVD,
	25,
	720, 576, 576,
	{ 6, 17, 22 },
	2,
	0,
	/* 9.52 Mb/s peak limit to keep bbdmux followed by mplex -f 8 happy */
	{ 0, 9000000, 9520000 }, /* 1 hour on 4.7 GB media */
	{ 1, 12, 3},
	/*
	 * Spatial filter = Manual, Temporal filter = Manual
	 * Median filter = Horizontal / Vertical
	 * Spatial filter value = 1, Temporal filter value = 4
	 */
	{ 0, 3, 1, 4 },
	48000
};


static const struct cxm_codec_profile
*codec_profiles[] = {
	&vcd_ntsc_profile,
	&vcd_pal_profile,
	&svcd_ntsc_profile,
	&svcd_pal_profile,
	&dvd_half_d1_ntsc_profile,
	&dvd_half_d1_pal_profile,
	&dvd_full_d1_ntsc_profile,
	&dvd_full_d1_pal_profile
};


static unsigned int
cxm_queue_firmware_command(struct cxm_softc *sc,
			    enum cxm_mailbox_name mbx_name, uint32_t cmd,
			    uint32_t *parameters, unsigned int nparameters)
{
	unsigned int i;
	unsigned int mailbox;
	uint32_t completed_command;
	uint32_t flags;

	if (nparameters > CXM_MBX_MAX_PARAMETERS) {
		device_printf(sc->dev, "too many parameters for mailbox\n");
		return -1;
	}

	mailbox = 0;

	switch (mbx_name) {
	case cxm_dec_mailbox:
		mailbox = sc->dec_mbx
			  + CXM_MBX_FW_CMD_MAILBOX *sizeof(struct cxm_mailbox);
		break;

	case cxm_enc_mailbox:
		mailbox = sc->enc_mbx
			  + CXM_MBX_FW_CMD_MAILBOX *sizeof(struct cxm_mailbox);
		break;

	default:
		return -1;
	}

	crit_enter();
	for (i = 0; i < CXM_MBX_FW_CMD_MAILBOXES; i++) {
		flags = CSR_READ_4(sc,
				   mailbox
				   + offsetof(struct cxm_mailbox, flags));
		if (!(flags & CXM_MBX_FLAG_IN_USE))
			break;

		/*
		 * Mail boxes containing certain completed commands
		 * for which the results are never needed can be reused.
		 */

		if ((flags & (CXM_MBX_FLAG_DRV_DONE | CXM_MBX_FLAG_FW_DONE))
		    == (CXM_MBX_FLAG_DRV_DONE | CXM_MBX_FLAG_FW_DONE)) {
			completed_command
			 = CSR_READ_4(sc,
				      mailbox
				      + offsetof(struct cxm_mailbox, command));

			/*
			 * DMA results are always check by reading the
			 * DMA status register ... never by checking
			 * the mailbox after the command has completed.
			 */

			if (completed_command == CXM_FW_CMD_SCHED_DMA_TO_HOST)
				break;
		}

		mailbox += sizeof(struct cxm_mailbox);
	}

	if (i >= CXM_MBX_FW_CMD_MAILBOXES) {
		crit_exit();
		return -1;
	}

	CSR_WRITE_4(sc, mailbox + offsetof(struct cxm_mailbox, flags),
		    CXM_MBX_FLAG_IN_USE);

	/*
	 * PCI writes may be buffered so force the
	 * write to complete by reading the last
	 * location written.
	 */

	CSR_READ_4(sc, mailbox + offsetof(struct cxm_mailbox, flags));

	crit_exit();

	CSR_WRITE_4(sc, mailbox + offsetof(struct cxm_mailbox, command), cmd);
	CSR_WRITE_4(sc, mailbox + offsetof(struct cxm_mailbox, timeout),
		    CXM_FW_STD_TIMEOUT);

	for (i = 0; i < nparameters; i++)
		CSR_WRITE_4(sc,
			    mailbox
			    + offsetof(struct cxm_mailbox, parameters)
			    + i * sizeof(uint32_t),
			    *(parameters + i));

	for (; i < CXM_MBX_MAX_PARAMETERS; i++)
		CSR_WRITE_4(sc,
			    mailbox
			    + offsetof(struct cxm_mailbox, parameters)
			    + i * sizeof(uint32_t), 0);

	CSR_WRITE_4(sc, mailbox + offsetof(struct cxm_mailbox, flags),
		    CXM_MBX_FLAG_IN_USE | CXM_MBX_FLAG_DRV_DONE);

	return mailbox;
}


static int
cxm_firmware_command(struct cxm_softc *sc,
		      enum cxm_mailbox_name mbx_name, uint32_t cmd,
		      uint32_t *parameters, unsigned int nparameters)
{
	const char *wmesg;
	unsigned int *bmp;
	unsigned int i;
	unsigned int mailbox;
	uint32_t flags;
	uint32_t result;

	bmp = NULL;
	wmesg = "";

	switch (mbx_name) {
	case cxm_dec_mailbox:
		bmp = &sc->dec_mbx;
		wmesg = "cxmdfw";
		break;

	case cxm_enc_mailbox:
		bmp = &sc->enc_mbx;
		wmesg = "cxmefw";
		break;

	default:
		return -1;
	}

	mailbox = cxm_queue_firmware_command(sc, mbx_name, cmd,
					     parameters, nparameters);
	if (mailbox == -1) {
		device_printf(sc->dev, "no free mailboxes\n");
		return -1;
	}

	/* Give the firmware a chance to start processing the request */
	tsleep(bmp, 0, wmesg, hz / 100);

	for (i = 0; i < 100; i++) {
		flags = CSR_READ_4(sc,
				   mailbox
				   + offsetof(struct cxm_mailbox, flags));
		if ((flags & CXM_MBX_FLAG_FW_DONE))
			break;

		/* Wait for 10ms */
		tsleep(bmp, 0, wmesg, hz / 100);
	}

	if (i >= 100) {
		device_printf(sc->dev, "timeout\n");
		return -1;
	}

	result = CSR_READ_4(sc,
			    mailbox
			    + offsetof(struct cxm_mailbox, result));

	for (i = 0; i < nparameters; i++)
		*(parameters + i)
		  = CSR_READ_4(sc,
			       mailbox
			       + offsetof(struct cxm_mailbox, parameters)
			       + i * sizeof(uint32_t));

	CSR_WRITE_4(sc, mailbox + offsetof(struct cxm_mailbox, flags), 0);

	return result == 0 ? 0 : -1;
}


static int
cxm_firmware_command_nosleep(struct cxm_softc *sc,
			      enum cxm_mailbox_name mbx_name, uint32_t cmd,
			      uint32_t *parameters, unsigned int nparameters)
{
	unsigned int i;
	unsigned int mailbox;
	uint32_t flags;
	uint32_t result;

	for (i = 0; i < 100; i++) {
		mailbox = cxm_queue_firmware_command(sc, mbx_name, cmd,
						     parameters, nparameters);
		if (mailbox != -1)
			break;

		/* Wait for 10ms */
		DELAY(10000);
		}

	if (i >= 100) {
		device_printf(sc->dev, "no free mailboxes\n");
		return -1;
	}

	/* Give the firmware a chance to start processing the request */
	DELAY(10000);

	for (i = 0; i < 100; i++) {
		flags = CSR_READ_4(sc,
				   mailbox
				   + offsetof(struct cxm_mailbox, flags));
		if ((flags & CXM_MBX_FLAG_FW_DONE))
			break;

		/* Wait for 10ms */
		DELAY(10000);
	}

	if (i >= 100) {
		device_printf(sc->dev, "timeout\n");
		return -1;
	}

	result = CSR_READ_4(sc,
			    mailbox
			    + offsetof(struct cxm_mailbox, result));

	for (i = 0; i < nparameters; i++)
		*(parameters + i)
		  = CSR_READ_4(sc,
			       mailbox
			       + offsetof(struct cxm_mailbox, parameters)
			       + i * sizeof(uint32_t));

	CSR_WRITE_4(sc, mailbox + offsetof(struct cxm_mailbox, flags), 0);

	return result == 0 ? 0 : -1;
}


static int
cxm_stop_firmware(struct cxm_softc *sc)
{

	if (cxm_firmware_command_nosleep(sc, cxm_enc_mailbox,
					 CXM_FW_CMD_ENC_HALT_FW, NULL, 0) < 0)
		return -1;

	if (sc->type == cxm_iTVC15_type
	    && cxm_firmware_command_nosleep(sc, cxm_dec_mailbox,
					    CXM_FW_CMD_DEC_HALT_FW,
					    NULL, 0) < 0)
		return -1;

	/* Wait for 10ms */
	DELAY(10000);

	return 0;
}


static void
cxm_set_irq_mask(struct cxm_softc *sc, uint32_t mask)
{
	crit_enter();

	CSR_WRITE_4(sc, CXM_REG_IRQ_MASK, mask);

	/*
	 * PCI writes may be buffered so force the
	 * write to complete by reading the last
	 * location written.
	 */

	CSR_READ_4(sc, CXM_REG_IRQ_MASK);

	sc->irq_mask = mask;

	crit_exit();
}


static void
cxm_set_irq_status(struct cxm_softc *sc, uint32_t status)
{

	CSR_WRITE_4(sc, CXM_REG_IRQ_STATUS, status);

	/*
	 * PCI writes may be buffered so force the
	 * write to complete by reading the last
	 * location written.
	 */

	CSR_READ_4(sc, CXM_REG_IRQ_STATUS);
}


static int
cxm_stop_hardware(struct cxm_softc *sc)
{
	if (sc->cxm_iic) {
		if (cxm_saa7115_mute(sc) < 0)
			return -1;
		if (cxm_msp_mute(sc) < 0)
			return -1;
	}

	/* Halt the firmware */
	if (sc->enc_mbx != -1) {
		if (cxm_stop_firmware(sc) < 0)
			return -1;
	}

	/* Mask all interrupts */
	cxm_set_irq_mask(sc, 0xffffffff);

	/* Stop VDM */
	CSR_WRITE_4(sc, CXM_REG_VDM, CXM_CMD_VDM_STOP);

	/* Stop AO */
	CSR_WRITE_4(sc, CXM_REG_AO, CXM_CMD_AO_STOP);

	/* Ping (?) APU */
	CSR_WRITE_4(sc, CXM_REG_APU, CXM_CMD_APU_PING);

	/* Stop VPU */
	CSR_WRITE_4(sc, CXM_REG_VPU, sc->type == cxm_iTVC15_type
					? CXM_CMD_VPU_STOP15
					: CXM_CMD_VPU_STOP16);

	/* Reset Hw Blocks */
	CSR_WRITE_4(sc, CXM_REG_HW_BLOCKS, CXM_CMD_HW_BLOCKS_RST);

	/* Stop SPU */
	CSR_WRITE_4(sc, CXM_REG_SPU, CXM_CMD_SPU_STOP);

	/* Wait for 10ms */
	DELAY(10000);

	return 0;
}


static int
cxm_download_firmware(struct cxm_softc *sc)
{
	unsigned int i;
	const uint32_t *fw;

	/* Check if firmware is compiled in */
	if (strncmp((const char *)cxm_enc_fw, "NOFW", 4) == 0) {
		device_printf(sc->dev, "encoder firmware not compiled in\n");
		return -1;
	} else if (strncmp((const char *)cxm_dec_fw, "NOFW", 4) == 0) {
		device_printf(sc->dev, "decoder firmware not compiled in\n");
		return -1;
	}

	/* Download the encoder firmware */
	fw = (const uint32_t *)cxm_enc_fw;
	for (i = 0; i < CXM_FW_SIZE; i += sizeof(*fw))
		CSR_WRITE_4(sc, CXM_MEM_ENC + i, *fw++);

	/* Download the decoder firmware */
	if (sc->type == cxm_iTVC15_type) {
		fw = (const uint32_t *)cxm_dec_fw;
		for (i = 0; i < CXM_FW_SIZE; i += sizeof(*fw))
			CSR_WRITE_4(sc, CXM_MEM_DEC + i, *fw++);
	}

	return 0;
}


static int
cxm_init_hardware(struct cxm_softc *sc)
{
	unsigned int i;
	unsigned int mailbox;
	uint32_t parameter;

	if (cxm_stop_hardware(sc) < 0)
		return -1;

	/* Initialize encoder SDRAM pre-charge */
	CSR_WRITE_4(sc, CXM_REG_ENC_SDRAM_PRECHARGE,
			CXM_CMD_SDRAM_PRECHARGE_INIT);

	/* Initialize encoder SDRAM refresh to 1us */
	CSR_WRITE_4(sc, CXM_REG_ENC_SDRAM_REFRESH,
			CXM_CMD_SDRAM_REFRESH_INIT);

	/* Initialize decoder SDRAM pre-charge */
	CSR_WRITE_4(sc, CXM_REG_DEC_SDRAM_PRECHARGE,
			CXM_CMD_SDRAM_PRECHARGE_INIT);

	/* Initialize decoder SDRAM refresh to 1us */
	CSR_WRITE_4(sc, CXM_REG_DEC_SDRAM_REFRESH,
			CXM_CMD_SDRAM_REFRESH_INIT);

	/* Wait for 600ms */
	DELAY(600000);

	if (cxm_download_firmware(sc) < 0)
		return -1;

	/* Enable SPU */
	CSR_WRITE_4(sc, CXM_REG_SPU,
			CSR_READ_4(sc, CXM_REG_SPU) & CXM_MASK_SPU_ENABLE);

	/* Wait for 1 second */
	DELAY(1000000);

	/* Enable VPU */
	CSR_WRITE_4(sc, CXM_REG_VPU,
			CSR_READ_4(sc, CXM_REG_VPU)
			& (sc->type == cxm_iTVC15_type
				? CXM_MASK_VPU_ENABLE15
				: CXM_MASK_VPU_ENABLE16));

	/* Wait for 1 second */
	DELAY(1000000);

	/* Locate encoder mailbox */
	mailbox = CXM_MEM_ENC;
	for (i = 0; i < CXM_MEM_ENC_SIZE; i += 0x100)
		if (CSR_READ_4(sc, mailbox + i) == 0x12345678
		    && CSR_READ_4(sc, mailbox + i + 4) == 0x34567812
		    && CSR_READ_4(sc, mailbox + i + 8) == 0x56781234
		    && CSR_READ_4(sc, mailbox + i + 12) == 0x78123456)
			break;

	if (i >= CXM_MEM_ENC_SIZE)
		return -1;

	sc->enc_mbx = mailbox + i + 16;

	/* Locate decoder mailbox */
	if (sc->type == cxm_iTVC15_type) {
		mailbox = CXM_MEM_DEC;
		for (i = 0; i < CXM_MEM_DEC_SIZE; i += 0x100)
			if (CSR_READ_4(sc, mailbox + i) == 0x12345678
			    && CSR_READ_4(sc, mailbox + i + 4) == 0x34567812
			    && CSR_READ_4(sc, mailbox + i + 8) == 0x56781234
			    && CSR_READ_4(sc, mailbox + i + 12) == 0x78123456)
				break;

		if (i >= CXM_MEM_DEC_SIZE)
			return -1;

		sc->dec_mbx = mailbox + i + 16;
	}

	/* Get encoder firmware version */
	parameter = 0;
	if (cxm_firmware_command_nosleep(sc, cxm_enc_mailbox,
					 CXM_FW_CMD_ENC_GET_FW_VER,
					 &parameter, 1) < 0)
		return -1;

	device_printf(sc->dev, "encoder firmware version %#x\n",
	    (unsigned int)parameter);

	/* Get decoder firmware version */
	if (sc->type == cxm_iTVC15_type) {
		parameter = 0;
		if (cxm_firmware_command_nosleep(sc, cxm_dec_mailbox,
						 CXM_FW_CMD_DEC_GET_FW_VER,
						 &parameter, 1) < 0)
			return -1;

		device_printf(sc->dev, "decoder firmware version %#x\n",
		    (unsigned int)parameter);
	}

	return 0;
}


static int
cxm_configure_encoder(struct cxm_softc *sc)
{
	int fps;
	unsigned int i;
	uint32_t parameters[12];
	const struct cxm_codec_profile *cpp;

	if (sc->source == cxm_fm_source)
		switch (cxm_tuner_selected_channel_set(sc)) {
		case CHNLSET_NABCST:
		case CHNLSET_CABLEIRC:
		case CHNLSET_JPNBCST:
		case CHNLSET_JPNCABLE:
			fps = 30;
			break;

		default:
			fps = 25;
			break;
		}
	else
		fps = cxm_saa7115_detected_fps(sc);

	if (fps < 0)
		return -1;

	if (sc->profile->fps != fps) {

		/*
		 * Pick a profile with the correct fps using the
		 * chosen stream type and width to decide between
		 * the VCD, SVCD, or DVD profiles.
		 */

		for (i = 0; i < NUM_ELEMENTS(codec_profiles); i++)
			if (codec_profiles[i]->fps == fps
			    && codec_profiles[i]->stream_type
			       == sc->profile->stream_type
			    && codec_profiles[i]->width == sc->profile->width)
				break;

		if (i >= NUM_ELEMENTS(codec_profiles))
			return -1;

		sc->profile = codec_profiles[i];
	}

	cpp = sc->profile;

	if (cxm_saa7115_configure(sc,
				  cpp->width, cpp->source_height, fps,
				  cpp->audio_sample_rate) < 0)
		return -1;

	/* assign dma block len */
	parameters[0] = 1; /* Transfer block size = 1 */
	parameters[1] = 1; /* Units = 1 (frames) */
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_DMA_BLOCKLEN,
				 parameters, 2) != 0)
		return -1;


	/* assign program index info */
	parameters[0] = 0; /* Picture mask = 0 (don't generate index) */
	parameters[1] = 0; /* Num_req = 0 */
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_PGM_INDEX_INFO,
				 parameters, 2) != 0)
		return -1;

	/* assign stream type */
	parameters[0] = cpp->stream_type;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_STREAM_TYPE,
				 parameters, 1) != 0)
		return -1;

	/* assign output port */
	parameters[0] = 0; /* 0 (Memory) */
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_OUTPUT_PORT,
				 parameters, 1) != 0)
		return -1;

	/* assign framerate */
	parameters[0] = cpp->fps == 30 ? 0 : 1;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_FRAME_RATE,
				 parameters, 1) != 0)
		return -1;

	/* assign frame size */
	parameters[0] = cpp->height;
	parameters[1] = cpp->width;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_FRAME_SIZE,
				 parameters, 2) != 0)
		return -1;

	/* assign aspect ratio */
	parameters[0] = cpp->aspect;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_ASPECT_RATIO,
				 parameters, 1) != 0)
		return -1;

	/* assign bitrates */
	parameters[0] = cpp->bitrate.mode;
	parameters[1] = cpp->bitrate.average;
	parameters[2] = cpp->bitrate.peak / 400;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_BITRATES,
				 parameters, 3) != 0)
		return -1;

	/* assign gop closure */
	parameters[0] = cpp->gop.closure;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_GOP_CLOSURE,
				 parameters, 1) != 0)
		return -1;

	/* assign gop properties */
	parameters[0] = cpp->gop.frames;
	parameters[1] = cpp->gop.bframes;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_GOP_PROPERTIES,
				 parameters, 2) != 0)
		return -1;

	/* assign 3 2 pulldown */
	parameters[0] = cpp->pulldown;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_3_2_PULLDOWN,
				 parameters, 1) != 0)
		return -1;

	/* assign dnr filter mode */
	parameters[0] = cpp->dnr.mode;
	parameters[1] = cpp->dnr.type;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_DNR_FILTER_MODE,
				 parameters, 2) != 0)
		return -1;

	/* assign dnr filter props */
	parameters[0] = cpp->dnr.spatial;
	parameters[1] = cpp->dnr.temporal;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_DNR_FILTER_PROPERTIES,
				 parameters, 2) != 0)
		return -1;

	/*
	 * assign audio properties
	 */

	for (i = 0; i < NUM_ELEMENTS(codec_audio_formats); i++)
		if (codec_audio_formats[i].sample_rate
		    == cpp->audio_sample_rate)
			break;

	if (i >= NUM_ELEMENTS(codec_audio_formats))
		return -1;

	parameters[0] = codec_audio_formats[i].format;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_AUDIO_PROPERTIES,
				 parameters, 1) != 0)
		return -1;

	/* assign coring levels */
	parameters[0] = 0; /* luma_h */
	parameters[1] = 255; /* luma_l */
	parameters[2] = 0; /* chroma_h */
	parameters[3] = 255; /* chroma_l */
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_CORING_LEVELS,
				 parameters, 4) != 0)
		return -1;

	/* assign spatial filter type */
	parameters[0] = 3; /* Luminance filter = 3 (2D H/V Separable) */
	parameters[1] = 1; /* Chrominance filter = 1 (1D Horizontal) */
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_SPATIAL_FILTER_TYPE,
				 parameters, 2) != 0)
		return -1;

	/* assign frame drop rate */
	parameters[0] = 0;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_FRAME_DROP_RATE,
				 parameters, 1) != 0)
		return -1;

	/* assign placeholder */
	parameters[0] = 0; /* type = 0 (Extension / UserData) */
	parameters[1] = 0; /* period */
	parameters[2] = 0; /* size_t */
	parameters[3] = 0; /* arg0 */
	parameters[4] = 0; /* arg1 */
	parameters[5] = 0; /* arg2 */
	parameters[6] = 0; /* arg3 */
	parameters[7] = 0; /* arg4 */
	parameters[8] = 0; /* arg5 */
	parameters[9] = 0; /* arg6 */
	parameters[10] = 0; /* arg7 */
	parameters[11] = 0; /* arg8 */
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_PLACEHOLDER,
				 parameters, 12) != 0)
		return -1;

	/* assign VBI properties */
	parameters[0] = 0xbd04; /* mode = 0 (sliced), stream and user data */
	parameters[1] = 0; /* frames per interrupt (only valid in raw mode) */
	parameters[2] = 0; /* total raw VBI frames (only valid in raw mode) */
	parameters[3] = 0x25256262; /* ITU 656 start codes (saa7115 table 24)*/
	parameters[4] = 0x38387f7f; /* ITU 656 stop codes (saa7115 table 24) */
	parameters[5] = cpp->vbi.nlines; /* lines per frame */
	parameters[6] = 1440; /* bytes per line = 720 pixels */
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_VBI_PROPERTIES,
				 parameters, 7) != 0)
		return -1;

	/* assign VBI lines */
	parameters[0] = 0xffffffff; /* all lines */
	parameters[1] = 0; /* disable VBI features */
	parameters[2] = 0;
	parameters[3] = 0;
	parameters[4] = 0;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_VBI_LINE,
				 parameters, 5) != 0)
		return -1;

	/* assign number of lines in fields 1 and 2 */
	parameters[0] = cpp->source_height / 2 + cpp->vbi.nlines;
	parameters[1] = cpp->source_height / 2 + cpp->vbi.nlines;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ASSIGN_NUM_VSYNC_LINES,
				 parameters, 2) != 0)
		return -1;

	return 0;
}


static int
cxm_start_encoder(struct cxm_softc *sc)
{
	uint32_t parameters[4];
	uint32_t subtype;
	uint32_t type;


	if (sc->encoding)
		return 0;

	if (cxm_configure_encoder(sc) < 0)
		return -1;

	/* Mute the video input if necessary. */
	parameters[0] = sc->source == cxm_fm_source ? 1 : 0;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_MUTE_VIDEO_INPUT,
				 parameters, 1) != 0)
		return -1;

	/* Clear pending encoder interrupts (which are currently masked) */
	cxm_set_irq_status(sc, CXM_IRQ_ENC);

	/* Enable event notification */
	parameters[0] = 0; /* Event = 0 (refresh encoder input) */
	parameters[1] = 1; /* Notification = 1 (enable) */
	parameters[2] = 0x10000000; /* Interrupt bit */
	parameters[3] = -1; /* Mailbox = -1 (no mailbox) */
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ENC_EVENT_NOTIFICATION,
				 parameters, 4) != 0)
		return -1;

	if (cxm_saa7115_mute(sc) < 0)
		return -1;
	if (cxm_msp_mute(sc) < 0)
		return -1;

	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_INITIALIZE_VIDEO_INPUT,
				 NULL, 0) < 0)
		return -1;

	if (cxm_saa7115_unmute(sc) < 0)
		return -1;
	if (cxm_msp_unmute(sc) < 0)
		return -1;

	/* Wait for 100ms */
	tsleep(&sc->encoding, 0, "cxmce", hz / 10);

	type = sc->mpeg ? CXM_FW_CAPTURE_STREAM_TYPE_MPEG
			: CXM_FW_CAPTURE_STREAM_TYPE_RAW;
	subtype = ((sc->mpeg || sc->source == cxm_fm_source)
		   ? CXM_FW_CAPTURE_STREAM_PCM_AUDIO : 0)
		  | ((sc->mpeg || sc->source != cxm_fm_source)
		     ? CXM_FW_CAPTURE_STREAM_YUV : 0);

	/* Start the encoder */
	parameters[0] = type;
	parameters[1] = subtype;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_BEGIN_CAPTURE, parameters, 2) != 0)
		return -1;

	sc->enc_pool.offset = 0;
	sc->enc_pool.read = 0;
	sc->enc_pool.write = 0;

	sc->encoding_eos = 0;

	sc->encoding = 1;

	/* Enable interrupts */
	cxm_set_irq_mask(sc, sc->irq_mask & ~CXM_IRQ_ENC);

	return 0;
}


static int
cxm_stop_encoder(struct cxm_softc *sc)
{
	uint32_t parameters[4];
	uint32_t subtype;
	uint32_t type;

	if (!sc->encoding)
		return 0;

	type = sc->mpeg ? CXM_FW_CAPTURE_STREAM_TYPE_MPEG
			: CXM_FW_CAPTURE_STREAM_TYPE_RAW;
	subtype = ((sc->mpeg || sc->source == cxm_fm_source)
		   ? CXM_FW_CAPTURE_STREAM_PCM_AUDIO : 0)
		  | ((sc->mpeg || sc->source != cxm_fm_source)
		     ? CXM_FW_CAPTURE_STREAM_YUV : 0);

	/* Stop the encoder */
	parameters[0] = sc->mpeg ? 0 : 1; /* When = 0 (end of GOP) */
	parameters[1] = type;
	parameters[2] = subtype;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_END_CAPTURE, parameters, 3) != 0)
		return -1;

	/* Wait for up to 1 second */
	crit_enter();
	if (!sc->encoding_eos)
		tsleep(&sc->encoding_eos, 0, "cxmeos", hz);
	crit_exit();

	if (sc->mpeg && !sc->encoding_eos)
		device_printf(sc->dev, "missing encoder EOS\n");

	/* Disable event notification */
	parameters[0] = 0; /* Event = 0 (refresh encoder input) */
	parameters[1] = 0; /* Notification = 0 (disable) */
	parameters[2] = 0x10000000; /* Interrupt bit */
	parameters[3] = -1; /* Mailbox = -1 (no mailbox) */
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_ENC_EVENT_NOTIFICATION,
				 parameters, 4) != 0)
		return -1;

	/* Disable interrupts */
	cxm_set_irq_mask(sc, sc->irq_mask | CXM_IRQ_ENC);

	sc->encoding = 0;

	return 0;
}


static int
cxm_pause_encoder(struct cxm_softc *sc)
{
	uint32_t parameter;

	/* Pause the encoder */
	parameter = 0;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_PAUSE_ENCODER, &parameter, 1) != 0)
		return -1;

	return 0;
}


static int
cxm_unpause_encoder(struct cxm_softc *sc)
{
	uint32_t parameter;

	/* Unpause the encoder */
	parameter = 1;
	if (cxm_firmware_command(sc, cxm_enc_mailbox,
				 CXM_FW_CMD_PAUSE_ENCODER, &parameter, 1) != 0)
		return -1;

	return 0;
}


static unsigned int
cxm_encoder_fixup_byte_order(struct cxm_softc *sc,
			      unsigned int current, size_t offset)
{
	unsigned int	strips;
	unsigned int	i;
	unsigned int	j;
	unsigned int	k;
	unsigned int	macroblocks_per_line;
	unsigned int	scratch;
	unsigned int	words_per_line;
	uint32_t	*ptr;
	uint32_t	*src;
	size_t		nbytes;

	switch (sc->enc_pool.bufs[current].byte_order) {
	case cxm_device_mpeg_byte_order:

		/*
		 * Convert each 32 bit word to the proper byte ordering.
		 */

		for (nbytes = 0,
		     ptr = (uint32_t *)sc->enc_pool.bufs[current].vaddr;
		     nbytes != sc->enc_pool.bufs[current].size;
		     nbytes += sizeof(*ptr), ptr++)
			*ptr = bswap32(*ptr);
		break;

	case cxm_device_yuv12_byte_order:

		/*
		 * Convert each macro block to planar using
		 * a scratch buffer (the buffer prior to the
		 * current buffer is always free since it marks
		 * the end of the ring buffer).
		 */

		scratch = (current + (CXM_SG_BUFFERS - 1)) % CXM_SG_BUFFERS;

		if (offset) {
			current = scratch;
			break;
		}

		src = (uint32_t *)sc->enc_pool.bufs[current].vaddr;
		words_per_line = sc->profile->width / sizeof(*ptr);
		macroblocks_per_line
		  = sc->profile->width / CXM_MACROBLOCK_WIDTH;
		strips = sc->enc_pool.bufs[current].size
			   / (macroblocks_per_line * CXM_MACROBLOCK_SIZE);

		for (i = 0; i < strips; i++) {
			ptr = (uint32_t *)sc->enc_pool.bufs[scratch].vaddr
			      + i * macroblocks_per_line * CXM_MACROBLOCK_SIZE
				/ sizeof(*ptr);
			for (j = 0; j < macroblocks_per_line; j++) {
				for (k = 0; k < CXM_MACROBLOCK_HEIGHT; k++) {
#if CXM_MACROBLOCK_WIDTH != 16
#  error CXM_MACROBLOCK_WIDTH != 16
#endif
					*(ptr + k * words_per_line)
					  = *src++;
					*(ptr + k * words_per_line + 1)
					  = *src++;
					*(ptr + k * words_per_line + 2)
					  = *src++;
					*(ptr + k * words_per_line + 3)
					  = *src++;
				}
				ptr += CXM_MACROBLOCK_WIDTH / sizeof(*ptr);
			}
		}

		sc->enc_pool.bufs[scratch].size
		  = sc->enc_pool.bufs[current].size;

		current = scratch;
		break;

	default:
		break;
	}

	sc->enc_pool.bufs[current].byte_order = cxm_host_byte_order;

	return current;
}


static void
cxm_encoder_dma_discard(struct cxm_softc *sc)
{
	uint32_t parameters[3];

	/* Discard the DMA request */
	parameters[0] = 0;
	parameters[1] = 0;
	parameters[2] = 0;
	if (cxm_queue_firmware_command(sc, cxm_enc_mailbox,
				       CXM_FW_CMD_SCHED_DMA_TO_HOST,
				       parameters, 3) == -1) {
		device_printf(sc->dev,
		    "failed to discard encoder dma request\n");
		return;
	}

	sc->encoding_dma = -1;
}


static void
cxm_encoder_dma_done(struct cxm_softc *sc)
{
	int buffers_pending;
	uint32_t status;

	if (!sc->encoding_dma) {
		device_printf(sc->dev,
		    "encoder dma not already in progress\n");
		return;
	}

	buffers_pending = sc->encoding_dma;
	sc->encoding_dma = 0;

	if (buffers_pending < 0)
		return;

	status = CSR_READ_4(sc, CXM_REG_DMA_STATUS) & 0x0000000f;

	if ((status
	     & (CXM_DMA_ERROR_LIST | CXM_DMA_ERROR_WRITE | CXM_DMA_SUCCESS))
	    != CXM_DMA_SUCCESS) {
		device_printf(sc->dev, "encoder dma status %#x\n",
		    (unsigned int)status);
		return;
	}

	/* Update the books */
	crit_enter();
	sc->enc_pool.write = (sc->enc_pool.write + buffers_pending)
				   % CXM_SG_BUFFERS;
	crit_exit();

	/* signal anyone requesting notification */
	if (sc->enc_proc)
		ksignal (sc->enc_proc, sc->enc_signal);

	/* wakeup anyone waiting for data */
	wakeup(&sc->enc_pool.read);

	/* wakeup anyone polling for data */
	KNOTE(&sc->enc_kq.ki_note, 0);
}


static void
cxm_encoder_dma_request(struct cxm_softc *sc)
{
	enum cxm_byte_order byte_order;
	int buffers_free;
	int buffers_pending;
	unsigned int current;
	unsigned int i;
	unsigned int mailbox;
	unsigned int macroblocks_per_line;
	unsigned int nrequests;
	unsigned int strips;
	uint32_t parameters[CXM_MBX_MAX_PARAMETERS];
	uint32_t type;
	size_t max_sg_segment;
	struct {
		size_t offset;
		size_t size;
	} requests[2];

	if (sc->encoding_dma) {
		device_printf(sc->dev, "encoder dma already in progress\n");
		cxm_encoder_dma_discard(sc);
		return;
	}

	mailbox = sc->enc_mbx
		  + CXM_MBX_FW_DMA_MAILBOX * sizeof(struct cxm_mailbox);

	for (i = 0; i < CXM_MBX_MAX_PARAMETERS; i++)
		parameters[i]
		  = CSR_READ_4(sc,
			       mailbox
			       + offsetof(struct cxm_mailbox, parameters)
			       + i * sizeof(uint32_t)
			      );

	byte_order = cxm_device_mpeg_byte_order;
	max_sg_segment = CXM_SG_SEGMENT;
	nrequests = 0;
	type = parameters[0];

	switch (type) {
	case 0: /* MPEG */
		requests[nrequests].offset = parameters[1];
		requests[nrequests++].size = parameters[2];
		break;

	case 1: /* YUV */
		byte_order = cxm_device_yuv12_byte_order;

		/*
		 * Simplify macroblock unpacking by ensuring
		 * that strips don't span buffers.
		 */

#if CXM_MACROBLOCK_SIZE % 256
#  error CXM_MACROBLOCK_SIZE not a multiple of 256
#endif

		macroblocks_per_line = sc->profile->width
				       / CXM_MACROBLOCK_WIDTH;
		strips = CXM_SG_SEGMENT
			 / (macroblocks_per_line * CXM_MACROBLOCK_SIZE);
		max_sg_segment = strips
				 * macroblocks_per_line * CXM_MACROBLOCK_SIZE;

		requests[nrequests].offset = parameters[1]; /* Y */
		requests[nrequests++].size = parameters[2];
		requests[nrequests].offset = parameters[3]; /* UV */
		requests[nrequests++].size = parameters[4];
		break;

	case 2: /* PCM (audio) */
	case 3: /* VBI */
	default:
		device_printf(sc->dev, "encoder dma type %#x unsupported\n",
		    (unsigned int)type);
		cxm_encoder_dma_discard(sc);
		return;
	}

	/*
	 * Determine the number of buffers free at this * instant *
	 * taking into consideration that the ring buffer wraps.
	 */
	crit_enter();
	buffers_free = sc->enc_pool.read - sc->enc_pool.write;
	if (buffers_free <= 0)
		buffers_free += CXM_SG_BUFFERS;
	crit_exit();

	/*
	 * Build the scatter / gather list taking in
	 * consideration that the ring buffer wraps,
	 * at least one free buffer must always be
	 * present to mark the end of the ring buffer,
	 * and each transfer must be a multiple of 256.
	 */

	buffers_pending = 0;
	current = sc->enc_pool.write;

	for (i = 0; i < nrequests; i++) {
		if (!requests[i].size) {
			device_printf(sc->dev, "encoder dma size is zero\n");
			cxm_encoder_dma_discard(sc);
			return;
		}

		while (requests[i].size) {
			sc->enc_pool.bufs[current].size
			  = requests[i].size > max_sg_segment
			    ? max_sg_segment : requests[i].size;
			sc->enc_pool.bufs[current].byte_order = byte_order;

			sc->enc_sg.vaddr[buffers_pending].src
			  = requests[i].offset;
			sc->enc_sg.vaddr[buffers_pending].dst
			  = sc->enc_pool.bufs[current].baddr;
			sc->enc_sg.vaddr[buffers_pending].size
			  = (sc->enc_pool.bufs[current].size + 0x000000ff)
			    & 0xffffff00;

			requests[i].offset += sc->enc_pool.bufs[current].size;
			requests[i].size -= sc->enc_pool.bufs[current].size;
			buffers_pending++;
			current = (current + 1) % CXM_SG_BUFFERS;

			if (buffers_pending >= buffers_free) {
				device_printf(sc->dev,
				    "encoder dma not enough buffer space free\n");
				cxm_encoder_dma_discard(sc);
				return;
			}
		}
	}

	/* Mark the last transfer in the list */
	sc->enc_sg.vaddr[buffers_pending - 1].size |= 0x80000000;

	/* Schedule the DMA */
	parameters[0] = sc->enc_sg.baddr;
	parameters[1] = buffers_pending * sizeof(sc->enc_sg.vaddr[0]);
	parameters[2] = type;
	if (cxm_queue_firmware_command(sc, cxm_enc_mailbox,
				       CXM_FW_CMD_SCHED_DMA_TO_HOST,
				       parameters, 3) == -1) {
		device_printf(sc->dev,
		    "failed to schedule encoder dma request\n");
		return;
	}

	/*
	 * Record the number of pending buffers for the
	 * benefit of cxm_encoder_dma_done.  Doing this
	 * after queuing the command doesn't introduce
	 * a race condition since we're already in the
	 * interrupt handler.
	 */

	sc->encoding_dma = buffers_pending;
}


static int
cxm_encoder_wait_for_lock(struct cxm_softc *sc)
{
	int muted;
	int locked;
	int result;

	locked = 1;

	/*
	 * Wait for the tuner to lock.
	 */
	if (sc->source == cxm_fm_source || sc->source == cxm_tuner_source) {
		result = cxm_tuner_wait_for_lock(sc);
		if (result <= 0)
			return result;
	}

	/*
	 * Wait for the video decoder to lock.
	 */
	if (sc->source != cxm_fm_source) {
		result = cxm_saa7115_wait_for_lock(sc);
		if (result < 0)
			return result;
		else if (result == 0)
			locked = 0;
		}

	/*
	 * Wait for the audio decoder to lock.
	 */
	if (sc->source == cxm_tuner_source) {
		muted = cxm_msp_is_muted(sc);

		result = cxm_msp_autodetect_standard(sc);
		if (result < 0)
			return result;
		else if (result == 0)
			locked = 0;

		if (muted == 0 && cxm_msp_unmute(sc) < 0)
			return -1;
	}

	return locked;
}


static void
cxm_mapmem(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *busaddrp;

	/*
	 * Only the first bus space address is needed
	 * since it's known that the memory is physically
	 * contiguous due to bus_dmamem_alloc.
	 */

	busaddrp = (bus_addr_t *)arg;
	*busaddrp = segs->ds_addr;
}


/*
 * the boot time probe routine.
 */
static int
cxm_probe(device_t dev)
{
	struct cxm_dev		*t;

	t = cxm_devs;

	while(t->name != NULL) {
		if ((pci_get_vendor(dev) == t->vid) &&
		    (pci_get_device(dev) == t->did)) {
			device_set_desc(dev, t->name);
			return 0;
		}
		t++;
	}

	return ENXIO;
}


/*
 * the attach routine.
 */
static int
cxm_attach(device_t dev)
{
	int		error;
	int		rid;
	int		unit;
	unsigned int	i;
	uint32_t	command;
	struct cxm_softc *sc;

	/* Get the device data */
	sc = device_get_softc(dev);
	unit = device_get_unit(dev);

	sc->dev = dev;
	sc->type = cxm_iTVC15_type;

	switch(pci_get_device(dev)) {
	case PCI_PRODUCT_ICOMPRESSION_ITVC16:
		sc->type = cxm_iTVC16_type;
		break;

	default:
		break;
	}

	/*
	 * Enable bus mastering and memory mapped I/O.
	 */
	pci_enable_busmaster(dev);
	pci_enable_io(dev, SYS_RES_MEMORY);
	command = pci_read_config(dev, PCIR_COMMAND, 4);

	if (!(command & PCIM_CMD_MEMEN)) {
		device_printf(dev, "failed to enable memory mappings\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Map control/status registers.
	 */
	rid = CXM_RID;
	sc->mem_res = bus_alloc_resource(dev, SYS_RES_MEMORY, &rid,
					0, ~0, 1, RF_ACTIVE);

	if (!sc->mem_res) {
		device_printf(dev, "could not map memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->btag = rman_get_bustag(sc->mem_res);
	sc->bhandle = rman_get_bushandle(sc->mem_res);

	/*
	 * Attach the I2C bus.
	 */
	sc->cxm_iic = device_add_child(dev, "cxm_iic", unit);

	if (!sc->cxm_iic) {
		device_printf(dev, "could not add cxm_iic\n");
		error = ENXIO;
		goto fail;
	}

	error = device_probe_and_attach(sc->cxm_iic);

	if (error) {
		device_printf(dev, "could not attach cxm_iic\n");
		goto fail;
	}

	/*
	 * Initialize the tuner.
	 */
	if (cxm_tuner_init(sc) < 0) {
		device_printf(dev, "could not initialize tuner\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Initialize the SAA7115.
	 */
	if (cxm_saa7115_init(sc) < 0) {
		device_printf(dev, "could not initialize video decoder\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Initialize the MSP3400.
	 */
	if (cxm_msp_init(sc) < 0) {
		device_printf(dev, "could not initialize audio decoder\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Initialize the IR Remote.
	 */
	if (cxm_ir_init(sc) < 0) {
		device_printf(dev, "could not initialize IR remote\n");
		error = ENXIO;
		goto fail;
	}

	sc->dec_mbx = -1;
	sc->enc_mbx = -1;

	/*
	 * Disable the Conexant device.
	 *
	 * This is done * after * attaching the I2C bus so
	 * cxm_stop_hardware can mute the video and audio
	 * decoders.
	 */
	cxm_stop_hardware(sc);

	/*
	 * Allocate our interrupt.
	 */
	rid = 0;
	sc->irq_res = bus_alloc_resource(dev, SYS_RES_IRQ, &rid,
				0, ~0, 1, RF_SHAREABLE | RF_ACTIVE);

	if (sc->irq_res == NULL) {
		device_printf(dev, "could not map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	error = bus_setup_intr(dev, sc->irq_res, 0,
			       cxm_intr, sc, &sc->ih_cookie, NULL);
	if (error) {
		device_printf(dev, "could not setup irq\n");
		goto fail;

	}

	/*
	 * Allocate a DMA tag for the parent bus.
	 */
	error = bus_dma_tag_create(NULL, 1, 0,
				   BUS_SPACE_MAXADDR_32BIT,
				   BUS_SPACE_MAXADDR,
				   BUS_SPACE_MAXSIZE_32BIT, 1,
				   BUS_SPACE_MAXSIZE_32BIT, 0,
				   &sc->parent_dmat);
	if (error) {
		device_printf(dev, "could not create parent bus DMA tag\n");
		goto fail;
	}

	/*
	 * Allocate a DMA tag for the encoder buffers.
	 */
	error = bus_dma_tag_create(sc->parent_dmat, 256, 0,
				   BUS_SPACE_MAXADDR_32BIT,
				   BUS_SPACE_MAXADDR,
				   CXM_SG_SEGMENT, 1,
				   BUS_SPACE_MAXSIZE_32BIT, 0,
				   &sc->enc_pool.dmat);
	if (error) {
		device_printf(dev,
			      "could not create encoder buffer DMA tag\n");
		goto fail;
	}

	for (i = 0; i < CXM_SG_BUFFERS; i++) {

		/*
		 * Allocate the encoder buffer.
		 */
		error = bus_dmamem_alloc(sc->enc_pool.dmat,
					 (void **)&sc->enc_pool.bufs[i].vaddr,
					 BUS_DMA_NOWAIT,
					 &sc->enc_pool.bufs[i].dmamap);
		if (error) {
			device_printf(dev,
				      "could not allocate encoder buffer\n");
			goto fail;
		}

		/*
		 * Map the encoder buffer.
		 */
		error = bus_dmamap_load(sc->enc_pool.dmat,
					sc->enc_pool.bufs[i].dmamap,
					sc->enc_pool.bufs[i].vaddr,
					CXM_SG_SEGMENT,
					cxm_mapmem,
					&sc->enc_pool.bufs[i].baddr, 0);
		if (error) {
			device_printf(dev, "could not map encoder buffer\n");
			goto fail;
		}
	}

	/*
	 * Allocate a DMA tag for the scatter / gather list.
	 */
	error = bus_dma_tag_create(sc->parent_dmat, 1, 0,
				   BUS_SPACE_MAXADDR_32BIT,
				   BUS_SPACE_MAXADDR,
				   CXM_SG_BUFFERS
				   * sizeof(struct cxm_sg_entry), 1,
				   BUS_SPACE_MAXSIZE_32BIT, 0,
				   &sc->enc_sg.dmat);
	if (error) {
		device_printf(dev,
			      "could not create scatter / gather DMA tag\n");
		goto fail;
	}

	/*
	 * Allocate the scatter / gather list.
	 */
	error = bus_dmamem_alloc(sc->enc_sg.dmat, (void **)&sc->enc_sg.vaddr,
				 BUS_DMA_NOWAIT, &sc->enc_sg.dmamap);
	if (error) {
		device_printf(dev,
			      "could not allocate scatter / gather list\n");
		goto fail;
	}

	/*
	 * Map the scatter / gather list.
	 */
	error = bus_dmamap_load(sc->enc_sg.dmat, sc->enc_sg.dmamap,
				sc->enc_sg.vaddr,
				CXM_SG_BUFFERS * sizeof(struct cxm_sg_entry),
				cxm_mapmem, &sc->enc_sg.baddr, 0);
	if (error) {
		device_printf(dev, "could not map scatter / gather list\n");
		goto fail;
	}

	/*
	 * Initialize the hardware.
	 */
	if (cxm_init_hardware(sc) < 0) {
		device_printf(dev, "could not initialize hardware\n");
		error = ENXIO;
		goto fail;
	}

	sc->profile = &dvd_full_d1_ntsc_profile;

	sc->source = cxm_tuner_source;


	/* make the device entries */
	sc->cxm_dev_t = make_dev(&cxm_ops, unit,
				 0, 0, 0444, "cxm%d",  unit);

	return 0;

fail:
	if (sc->enc_sg.baddr)
		bus_dmamap_unload(sc->enc_sg.dmat, sc->enc_sg.dmamap);
	if (sc->enc_sg.vaddr)
		bus_dmamem_free(sc->enc_sg.dmat, sc->enc_sg.vaddr,
				sc->enc_sg.dmamap);
	if (sc->enc_sg.dmat)
		bus_dma_tag_destroy(sc->enc_sg.dmat);

	for (i = 0; i < CXM_SG_BUFFERS; i++) {
		if (sc->enc_pool.bufs[i].baddr)
			bus_dmamap_unload(sc->enc_pool.dmat,
					  sc->enc_pool.bufs[i].dmamap);
		if (sc->enc_pool.bufs[i].vaddr)
			bus_dmamem_free(sc->enc_pool.dmat,
					sc->enc_pool.bufs[i].vaddr,
					sc->enc_pool.bufs[i].dmamap);
	}

	if (sc->enc_pool.dmat)
		bus_dma_tag_destroy(sc->enc_pool.dmat);

	if (sc->parent_dmat)
		bus_dma_tag_destroy(sc->parent_dmat);

	/*
	 * Detach the I2C bus.
	 *
	 * This is done * after * deallocating the scatter / gather
	 * list and buffers so the kernel has a better chance of
	 * gracefully handling a memory shortage.
	 *
	 * Detach the children before recursively deleting
	 * in case a child has a pointer to a grandchild
	 * which is used by the child's detach routine.
	 */
	bus_generic_detach(dev);
	if (sc->cxm_iic)
		device_delete_child(dev, sc->cxm_iic);

	if (sc->ih_cookie)
		bus_teardown_intr(dev, sc->irq_res, sc->ih_cookie);
	if (sc->irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, CXM_RID, sc->mem_res);

	return error;
}

/*
 * the detach routine.
 */
static int
cxm_detach(device_t dev)
{
	unsigned int i;
	struct cxm_softc *sc;
	device_t child;

	/* Get the device data */
	sc = device_get_softc(dev);

	/* Disable the Conexant device. */
	cxm_stop_hardware(sc);

	/* Unregister the /dev/cxmN device. */
	dev_ops_remove_minor(&cxm_ops, /*0, */device_get_unit(dev));

	/*
	 * Deallocate scatter / gather list and buffers.
	 */
	bus_dmamap_unload(sc->enc_sg.dmat, sc->enc_sg.dmamap);
	bus_dmamem_free(sc->enc_sg.dmat, sc->enc_sg.vaddr, sc->enc_sg.dmamap);

	bus_dma_tag_destroy(sc->enc_sg.dmat);

	for (i = 0; i < CXM_SG_BUFFERS; i++) {
		bus_dmamap_unload(sc->enc_pool.dmat,
				  sc->enc_pool.bufs[i].dmamap);
		bus_dmamem_free(sc->enc_pool.dmat, sc->enc_pool.bufs[i].vaddr,
				sc->enc_pool.bufs[i].dmamap);
	}

	bus_dma_tag_destroy(sc->enc_pool.dmat);

	bus_dma_tag_destroy(sc->parent_dmat);

	/*
	 * Detach the I2C bus.
	 *
	 * This is done * after * deallocating the scatter / gather
	 * list and buffers so the kernel has a better chance of
	 * gracefully handling a memory shortage.
	 *
	 * Detach the children before recursively deleting
	 * in case a child has a pointer to a grandchild
	 * which is used by the child's detach routine.
	 *
	 * Remember the child before detaching so we can
	 * delete it (bus_generic_detach indirectly zeroes
	 * sc->child_dev).
	 */
	child = sc->cxm_iic;
	bus_generic_detach(dev);
	if (child)
		device_delete_child(dev, child);

	/* Deallocate resources. */
	bus_teardown_intr(dev, sc->irq_res, sc->ih_cookie);
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	bus_release_resource(dev, SYS_RES_MEMORY, CXM_RID, sc->mem_res);

	return 0;
}

/*
 * the shutdown routine.
 */
static int
cxm_shutdown(device_t dev)
{
	struct cxm_softc *sc = device_get_softc(dev);

	/* Disable the Conexant device. */
	cxm_stop_hardware(sc);

	return 0;
}

/*
 * the interrupt routine.
 */
static void
cxm_intr(void *arg)
{
	uint32_t status;
	struct cxm_softc *sc;

	/* Get the device data */
	sc = (struct cxm_softc *)arg;

	status = CSR_READ_4(sc, CXM_REG_IRQ_STATUS);

	status &= ~sc->irq_mask;

	if (!status)
		return;

	/* Process DMA done before handling a new DMA request or EOS */
	if (status & CXM_IRQ_ENC_DMA_DONE)
		cxm_encoder_dma_done(sc);

	if (status & CXM_IRQ_ENC_DMA_REQUEST)
		cxm_encoder_dma_request(sc);

	if (status & CXM_IRQ_ENC_EOS) {
		sc->encoding_eos = 1;
		wakeup(&sc->encoding_eos);
	}

	cxm_set_irq_status(sc, status);
}


/*
 * the child detached routine.
 */
static void
cxm_child_detached(device_t dev, device_t child)
{
	struct cxm_softc *sc;

	/* Get the device data */
	sc = device_get_softc(dev);

	if (child == sc->cxm_iic)
		sc->cxm_iic = NULL;
}


static int
cxm_read_ivar(device_t dev, device_t child, int index, uintptr_t* val)
{
	struct cxm_softc *sc;

	/* Get the device data */
	sc = device_get_softc(dev);

	switch (index) {
	case CXM_IVAR_BHANDLE:
		*(bus_space_handle_t **)val = &sc->bhandle;
		break;

	case CXM_IVAR_BTAG:
		*(bus_space_tag_t **)val = &sc->btag;
		break;

	case CXM_IVAR_IICBUS:
		*(device_t **)val = &sc->iicbus;
		break;

	default:
		return ENOENT;
	}

	return 0;
}


static int
cxm_write_ivar(device_t dev, device_t child, int index, uintptr_t val)
{
	struct cxm_softc *sc;

	/* Get the device data */
	sc = device_get_softc(dev);

	switch (index) {
	case CXM_IVAR_BHANDLE:
		return EINVAL;

	case CXM_IVAR_BTAG:
		return EINVAL;

	case CXM_IVAR_IICBUS:
		if (sc->iicbus)
			return EINVAL;
		sc->iicbus = val ? *(device_t *)val : NULL;
		break;

	default:
		return ENOENT;
	}

	return 0;
}


/*---------------------------------------------------------
**
**	Conexant iTVC15 / iTVC16 character device driver routines
**
**---------------------------------------------------------
*/

#define UNIT(x)		((x) & 0x0f)
#define FUNCTION(x)	(x >> 4)

/*
 *
 */
static int
cxm_open(struct dev_open_args *ap)
{
	cdev_t		dev = ap->a_head.a_dev;
	int		unit;
	struct cxm_softc *sc;

	unit = UNIT(minor(dev));

	/* Get the device data */
	sc = (struct cxm_softc*)devclass_get_softc(cxm_devclass, unit);
	if (sc == NULL) {
		/* the device is no longer valid/functioning */
		return ENXIO;
	}

	if (sc->is_opened)
		return EBUSY;

	sc->is_opened = 1;
	sc->mpeg = 1;

	/* Record that the device is now busy */
	device_busy(devclass_get_device(cxm_devclass, unit));

	return 0;
}


/*
 *
 */
static int
cxm_close(struct dev_close_args *ap)
{
	cdev_t		dev = ap->a_head.a_dev;
	int		unit;
	struct cxm_softc *sc;

	unit = UNIT(minor(dev));

	/* Get the device data */
	sc = (struct cxm_softc*)devclass_get_softc(cxm_devclass, unit);
	if (sc == NULL) {
		/* the device is no longer valid/functioning */
		return ENXIO;
	}

	if (cxm_stop_encoder(sc) < 0)
		return ENXIO;

	sc->enc_pool.offset = 0;
	sc->enc_pool.read = 0;
	sc->enc_pool.write = 0;

	sc->enc_proc = NULL;
	sc->enc_signal = 0;

	device_unbusy(devclass_get_device(cxm_devclass, unit));

	sc->is_opened = 0;

	return 0;
}


/*
 *
 */
static int
cxm_read(struct dev_read_args *ap)
{
	cdev_t		dev = ap->a_head.a_dev;
	int		buffers_available;
	int		buffers_read;
	int		error;
	int		unit;
	unsigned int	current;
	unsigned int	i;
	size_t		nbytes;
	size_t		offset;
	struct cxm_softc *sc;

	unit = UNIT(minor(dev));

	/* Get the device data */
	sc = (struct cxm_softc*)devclass_get_softc(cxm_devclass, unit);
	if (sc == NULL) {
		/* the device is no longer valid/functioning */
		return ENXIO;
	}

	/* Only trigger the encoder if the ring buffer is empty */
	if (!sc->encoding && sc->enc_pool.read == sc->enc_pool.write) {
		if (cxm_start_encoder(sc) < 0)
			return ENXIO;
		if (ap->a_ioflag & IO_NDELAY)
			return EWOULDBLOCK;
	}

	buffers_available = 0;

	crit_enter();
	while (sc->enc_pool.read == sc->enc_pool.write) {
		error = tsleep(&sc->enc_pool.read, PCATCH, "cxmrd", 0);
		if (error) {
			crit_exit();
			return error;
		}
	}

	/*
	 * Determine the number of buffers available at this * instant *
	 * taking in consideration that the ring buffer wraps.
	 */
	buffers_available = sc->enc_pool.write - sc->enc_pool.read;
	if (buffers_available < 0)
		buffers_available += CXM_SG_BUFFERS;
	crit_exit();

	offset = sc->enc_pool.offset;

	for (buffers_read = 0, i = sc->enc_pool.read;
	     buffers_read != buffers_available && ap->a_uio->uio_resid;
	     buffers_read++, i = (i + 1) % CXM_SG_BUFFERS) {

		current = cxm_encoder_fixup_byte_order (sc, i, offset);

		nbytes = sc->enc_pool.bufs[current].size - offset;

		/* Don't transfer more than requested */
		if (nbytes > ap->a_uio->uio_resid)
			nbytes = ap->a_uio->uio_resid;

		error = uiomove(sc->enc_pool.bufs[current].vaddr + offset,
				nbytes, ap->a_uio);
		if (error)
			return error;

		offset += nbytes;

		/* Handle a partial read of a buffer */
		if (!ap->a_uio->uio_resid && offset != sc->enc_pool.bufs[i].size)
			break;

		offset = 0;
	}

	sc->enc_pool.offset = offset;

	/* Update the books */
	crit_enter();
	sc->enc_pool.read = (sc->enc_pool.read + buffers_read)
			    % CXM_SG_BUFFERS;
	crit_exit();

	return 0;
}


/*
 *
 */
static int
cxm_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t		dev = ap->a_head.a_dev;
	int		brightness;
	int		chroma_saturation;
	int		contrast;
	int		fps;
	int		hue;
	int		result;
	int		status;
	int		unit;
	unsigned int	i;
	unsigned int	sig;
	unsigned long	freq;
	struct cxm_softc *sc;
	enum cxm_source	source;
	struct bktr_capture_area *cap;
	struct bktr_remote *remote;

	unit = UNIT(minor(dev));

	/* Get the device data */
	sc = (struct cxm_softc*)devclass_get_softc(cxm_devclass, unit);
	if (sc == NULL) {
		/* the device is no longer valid/functioning */
		return ENXIO;
	}

	switch (ap->a_cmd) {
	case BT848_GAUDIO:
		switch (cxm_msp_selected_source(sc)) {
		case cxm_tuner_source:
			*(int *) ap->a_data = AUDIO_TUNER;
			break;

		case cxm_line_in_source_composite:
		case cxm_line_in_source_svideo:
			*(int *) ap->a_data = AUDIO_EXTERN;
			break;

		case cxm_fm_source:
			*(int *) ap->a_data = AUDIO_INTERN;
			break;

		default:
			return ENXIO;
		}

		if (cxm_msp_is_muted(sc) == 1)
			*(int *) ap->a_data |= AUDIO_MUTE;
		break;

	case BT848_SAUDIO:
		source = cxm_unknown_source;

		switch (*(int *) ap->a_data) {
		case AUDIO_TUNER:
			source = cxm_tuner_source;
			break;

		case AUDIO_EXTERN:
			source = cxm_line_in_source_composite;
			break;

		case AUDIO_INTERN:
			source = cxm_fm_source;
			break;

		case AUDIO_MUTE:
			if (cxm_msp_mute(sc) < 0)
				return ENXIO;
			return 0;

		case AUDIO_UNMUTE:
			if (cxm_msp_unmute(sc) < 0)
				return ENXIO;
			return 0;

		default:
			return EINVAL;
		}

		if (sc->encoding) {

			/*
			 * Switching between audio + video and audio only
			 * subtypes isn't supported while encoding.
			 */

			if (source != sc->source
			    && (source == cxm_fm_source
				|| sc->source == cxm_fm_source))
				return EBUSY;
		}

		if (cxm_pause_encoder(sc) < 0)
			return ENXIO;

		if (cxm_msp_select_source(sc, source) < 0)
			return ENXIO;

		if (source == cxm_fm_source)
			sc->source = source;

		result = cxm_encoder_wait_for_lock(sc);
		if (result < 0)
			return ENXIO;
		else if (result == 0)
			return EINVAL;

		if (cxm_unpause_encoder(sc) < 0)
			return ENXIO;
		break;

	case BT848_GBRIG:
		brightness = cxm_saa7115_get_brightness(sc);

		if (brightness < 0)
			return ENXIO;

		/*
		 * Brooktree brightness:
		 * 0x80 = -50.0%, 0x00 = +0.0%, 0x7f = +49.6%
		 */
		*(int *)ap->a_data = (int)(unsigned char)brightness - 128;
		break;

	case BT848_SBRIG:

		/*
		 * Brooktree brightness:
		 * 0x80 = -50.0%, 0x00 = +0.0%, 0x7f = +49.6%
		 */
		brightness = *(int *)ap->a_data + 128;

		if (cxm_saa7115_set_brightness(sc, brightness) < 0)
			return ENXIO;
		break;

	case METEORGBRIG:
		brightness = cxm_saa7115_get_brightness(sc);

		if (brightness < 0)
			return ENXIO;

		*(unsigned char *)ap->a_data = (unsigned char)brightness;
		break;

	case METEORSBRIG:
		brightness = *(unsigned char *)ap->a_data;

		if (cxm_saa7115_set_brightness(sc, brightness) < 0)
			return ENXIO;
		break;

	case BT848_GCSAT:
		chroma_saturation = cxm_saa7115_get_chroma_saturation(sc);

		if (chroma_saturation < 0)
			return ENXIO;

		/*
		 * Brooktree chroma saturation:
		 * 0x000 = 0%, 0x0fe = 100%, 0x1ff = 201.18%
		 */
		*(int *)ap->a_data = ((signed char)chroma_saturation > 0)
				? (chroma_saturation * 4 - 2) : 0;
		break;

	case BT848_SCSAT:

		/*
		 * Brooktree chroma saturation:
		 * 0x000 = 0%, 0x0fe = 100%, 0x1ff = 201.18%
		 */
		chroma_saturation = (*(int *)ap->a_data & 0x1ff) < 510
				      ? ((*(int *)ap->a_data & 0x1ff) + 2) / 4 : 127;

		if (cxm_saa7115_set_chroma_saturation(sc, chroma_saturation)
		    < 0)
			return ENXIO;

		break;

	case METEORGCSAT:
		chroma_saturation = cxm_saa7115_get_chroma_saturation(sc);

		if (chroma_saturation < 0)
			return ENXIO;

		*(unsigned char *)ap->a_data = (unsigned char)chroma_saturation;
		break;

	case METEORSCSAT:
		chroma_saturation = *(unsigned char *)ap->a_data;

		if (cxm_saa7115_set_chroma_saturation(sc, chroma_saturation)
		    < 0)
			return ENXIO;
		break;

	case METEORGCONT:
		contrast = cxm_saa7115_get_contrast(sc);

		if (contrast < 0)
			return ENXIO;

		*(unsigned char *)ap->a_data = (unsigned char)contrast;
		break;

	case METEORSCONT:
		contrast = *(unsigned char *)ap->a_data;

		if (cxm_saa7115_set_contrast(sc, contrast) < 0)
			return ENXIO;
		break;

	case BT848_GHUE:
		hue = cxm_saa7115_get_hue(sc);

		if (hue < 0)
			return ENXIO;

		*(int *)ap->a_data = (signed char)hue;
		break;

	case BT848_SHUE:
		hue = *(int *)ap->a_data;

		if (cxm_saa7115_set_hue(sc, hue) < 0)
			return ENXIO;
		break;

	case METEORGHUE:
		hue = cxm_saa7115_get_hue(sc);

		if (hue < 0)
			return ENXIO;

		*(signed char *)ap->a_data = (signed char)hue;
		break;

	case METEORSHUE:
		hue = *(signed char *)ap->a_data;

		if (cxm_saa7115_set_hue(sc, hue) < 0)
			return ENXIO;
		break;

	case METEORCAPTUR:
		switch (*(int *) ap->a_data) {
		case METEOR_CAP_CONTINOUS:
			if (cxm_start_encoder(sc) < 0)
				return ENXIO;
			break;

		case METEOR_CAP_STOP_CONT:
			if (cxm_stop_encoder(sc) < 0)
				return ENXIO;
			break;

		default:
			return EINVAL;
		}
		break;

	case BT848_GCAPAREA:
		cap = (struct bktr_capture_area *)ap->a_data;
		memset (cap, 0, sizeof (*cap));
		cap->x_offset = 0;
		cap->y_offset = 0;
		cap->x_size = sc->profile->width;
		cap->y_size = sc->profile->height;
		break;

	case BT848_SCAPAREA:
		if (sc->encoding)
			return EBUSY;

		cap = (struct bktr_capture_area *)ap->a_data;
		if (cap->x_offset || cap->y_offset
		    || (cap->x_size % CXM_MACROBLOCK_WIDTH)
		    || (cap->y_size % CXM_MACROBLOCK_HEIGHT))
			return EINVAL;

		/*
		 * Setting the width and height has the side effect of
		 * chosing between the VCD, SVCD, and DVD profiles.
		 */

		for (i = 0; i < NUM_ELEMENTS(codec_profiles); i++)
			if (codec_profiles[i]->width == cap->x_size
			    && codec_profiles[i]->height == cap->y_size)
				break;

		if (i >= NUM_ELEMENTS(codec_profiles))
			return EINVAL;

		sc->profile = codec_profiles[i];
		break;

	case BT848GFMT:
		switch (cxm_saa7115_detected_format(sc)) {
		case cxm_ntsc_60hz_source_format:
			*(unsigned long *)ap->a_data = BT848_IFORM_F_NTSCM;
			break;

		case cxm_pal_50hz_source_format:
			*(unsigned long *)ap->a_data = BT848_IFORM_F_PALBDGHI;
			break;

		case cxm_secam_50hz_source_format:
			*(unsigned long *)ap->a_data = BT848_IFORM_F_SECAM;
			break;

		case cxm_pal_60hz_source_format:
			*(unsigned long *)ap->a_data = BT848_IFORM_F_PALM;
			break;

		case cxm_bw_50hz_source_format:
		case cxm_bw_60hz_source_format:
		case cxm_ntsc_50hz_source_format:
			*(unsigned long *)ap->a_data = BT848_IFORM_F_AUTO;
			break;

		default:
			return ENXIO;
		}
		break;

	case METEORGFMT:
		switch (cxm_saa7115_detected_format(sc)) {
		case cxm_ntsc_60hz_source_format:
			*(unsigned long *)ap->a_data = METEOR_FMT_NTSC;
			break;

		case cxm_pal_50hz_source_format:
			*(unsigned long *)ap->a_data = METEOR_FMT_PAL;
			break;

		case cxm_secam_50hz_source_format:
			*(unsigned long *)ap->a_data = METEOR_FMT_SECAM;
			break;

		case cxm_bw_50hz_source_format:
		case cxm_bw_60hz_source_format:
		case cxm_ntsc_50hz_source_format:
		case cxm_pal_60hz_source_format:
			*(unsigned long *)ap->a_data = METEOR_FMT_AUTOMODE;
			break;

		default:
			return ENXIO;
		}
		break;

	case METEORGFPS:
		fps = cxm_saa7115_detected_fps(sc);

		if (fps < 0)
			return ENXIO;

		*(unsigned short *)ap->a_data = fps;
		break;

	case METEORGINPUT:
		switch (sc->source) {
		case cxm_tuner_source:
			*(unsigned long *)ap->a_data = METEOR_INPUT_DEV1;
			break;

		case cxm_line_in_source_composite:
			*(unsigned long *)ap->a_data = METEOR_INPUT_DEV2;
			break;

		case cxm_line_in_source_svideo:
			*(unsigned long *)ap->a_data = METEOR_INPUT_DEV_SVIDEO;
			break;

		default:
			return ENXIO;
		}
		break;

	case METEORSINPUT:
		source = cxm_unknown_source;

		switch (*(unsigned long *)ap->a_data & 0xf000) {
		case METEOR_INPUT_DEV1:
			source = cxm_tuner_source;
			break;

		case METEOR_INPUT_DEV2:
			source = cxm_line_in_source_composite;
			break;

		case METEOR_INPUT_DEV_SVIDEO:
			source = cxm_line_in_source_svideo;
			break;

		default:
			 return EINVAL;
		}

		if (sc->encoding) {

			/*
			 * Switching between audio + video and audio only
			 * subtypes isn't supported while encoding.
			 */

			if (source != sc->source
			    && (source == cxm_fm_source
				|| sc->source == cxm_fm_source))
				return EBUSY;
		}

		if (cxm_pause_encoder(sc) < 0)
			return ENXIO;

		if (cxm_saa7115_select_source(sc, source) < 0)
			return ENXIO;
		if (cxm_msp_select_source(sc, source) < 0)
			return ENXIO;
		sc->source = source;

		result = cxm_encoder_wait_for_lock(sc);
		if (result < 0)
			return ENXIO;
		else if (result == 0)
			return EINVAL;

		if (cxm_unpause_encoder(sc) < 0)
			return ENXIO;
		break;

	case METEORGSIGNAL:
		*(unsigned int *)ap->a_data = sc->enc_signal;
		break;

	case METEORSSIGNAL:
		sig = *(unsigned int *)ap->a_data;

		if (!_SIG_VALID(sig))
			return EINVAL;

		/*
		 * Historically, applications used METEOR_SIG_MODE_MASK
		 * to reset signal delivery.
		 */
		if (sig == METEOR_SIG_MODE_MASK)
			sig = 0;

		crit_enter();
		sc->enc_proc = sig ? curproc : NULL;
		sc->enc_signal = sig;
		crit_exit();
		break;

	case RADIO_GETFREQ:
		/* Convert from kHz to MHz * 100 */
		freq = sc->tuner_freq / 10;

		*(unsigned int *)ap->a_data = freq;
		break;

	case RADIO_SETFREQ:
		if (sc->source == cxm_fm_source)
			if (cxm_pause_encoder(sc) < 0)
				return ENXIO;

		/* Convert from MHz * 100 to kHz */
		freq = *(unsigned int *)ap->a_data * 10;

		if (cxm_tuner_select_frequency(sc, cxm_tuner_fm_freq_type,
					       freq) < 0)
			return ENXIO;

		/*
		 * Explicitly wait for the tuner lock so we
		 * can indicate if there's a station present.
		 */
		if (cxm_tuner_wait_for_lock(sc) < 0)
			return EINVAL;

		result = cxm_encoder_wait_for_lock(sc);
		if (result < 0)
			return ENXIO;
		else if (result == 0)
			return EINVAL;

		if (sc->source == cxm_fm_source)
			if (cxm_unpause_encoder(sc) < 0)
				return ENXIO;
		break;

	case TVTUNER_GETAFC:
		*(int *)ap->a_data = sc->tuner_afc;
		break;

	case TVTUNER_SETAFC:
		sc->tuner_afc = (*(int *)ap->a_data != 0);
		break;

	case TVTUNER_GETTYPE:
		*(unsigned int *)ap->a_data = cxm_tuner_selected_channel_set(sc);
		break;

	case TVTUNER_SETTYPE:
		if (cxm_tuner_select_channel_set(sc, *(unsigned int *)ap->a_data) < 0)
			return EINVAL;
		break;

	case TVTUNER_SETCHNL:
		if (sc->source == cxm_tuner_source)
			if (cxm_pause_encoder(sc) < 0)
				return ENXIO;

		if (cxm_tuner_select_channel(sc, *(unsigned int *)ap->a_data) < 0)
			return ENXIO;

		if (sc->tuner_afc)
			if (cxm_tuner_apply_afc(sc) < 0)
				return EINVAL;

		/*
		 * Explicitly wait for the tuner lock so we
		 * can indicate if there's a station present.
		 */
		if (cxm_tuner_wait_for_lock(sc) < 0)
			return EINVAL;

		result = cxm_encoder_wait_for_lock(sc);
		if (result < 0)
			return ENXIO;
		else if (result == 0)
			return EINVAL;

		if (sc->source == cxm_tuner_source)
			if (cxm_unpause_encoder(sc) < 0)
				return ENXIO;
		break;

	case TVTUNER_GETFREQ:
		/* Convert from kHz to MHz * 16 */
		freq = (sc->tuner_freq * 16) / 1000;

		*(unsigned int *)ap->a_data = freq;
		break;

	case TVTUNER_SETFREQ:
		if (sc->source == cxm_tuner_source)
			if (cxm_pause_encoder(sc) < 0)
				return ENXIO;

		/* Convert from MHz * 16 to kHz */
		freq = (*(unsigned int *)ap->a_data * 1000) / 16;

		if (cxm_tuner_select_frequency(sc, cxm_tuner_tv_freq_type,
					       freq) < 0)
			return ENXIO;

		/*
		 * Explicitly wait for the tuner lock so we
		 * can indicate if there's a station present.
		 */
		if (cxm_tuner_wait_for_lock(sc) < 0)
			return EINVAL;

		result = cxm_encoder_wait_for_lock(sc);
		if (result < 0)
			return ENXIO;
		else if (result == 0)
			return EINVAL;

		if (sc->source == cxm_tuner_source)
			if (cxm_unpause_encoder(sc) < 0)
				return ENXIO;

		break;

	case TVTUNER_GETSTATUS:
		status = cxm_tuner_status(sc);
		if (status < 0)
			return ENXIO;
		*(unsigned long *)ap->a_data = status & 0xff;
		break;

	case REMOTE_GETKEY:
		remote = (struct bktr_remote *)ap->a_data;
		if (cxm_ir_key(sc, (char *)remote, sizeof(*remote)) < 0)
			return ENXIO;
		break;

	default:
		return ENOTTY;
	}

	return 0;
}

static struct filterops cxm_filterops =
	{ FILTEROP_ISFD, NULL, cxm_filter_detach, cxm_filter };

static int
cxm_kqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct cxm_softc *sc;
	struct klist *klist;
	int unit;

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		unit = UNIT(minor(dev));
		/* Get the device data */
		sc = (struct cxm_softc *)devclass_get_softc(cxm_devclass, unit);
		kn->kn_fop = &cxm_filterops;
		kn->kn_hook = (caddr_t)sc;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	klist = &sc->enc_kq.ki_note;
	knote_insert(klist, kn);

	return (0);
}

static void
cxm_filter_detach(struct knote *kn)
{
	struct cxm_softc *sc = (struct cxm_softc *)kn->kn_hook;
	struct klist *klist = &sc->enc_kq.ki_note;

	knote_remove(klist, kn);
}

static int
cxm_filter(struct knote *kn, long hint)
{
	struct cxm_softc *sc = (struct cxm_softc *)kn->kn_hook;
	int ready = 0;

	if (sc == NULL) {
		/* the device is no longer valid/functioning */
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		return (1);
	}

	crit_enter();
	if (sc->enc_pool.read != sc->enc_pool.write)
		ready = 1;
	crit_exit();

	return (ready);
}
