/*-
 * Copyright (c) 2006 IronPort Systems
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
/*-
 * Copyright (c) 2007 LSI Corp.
 * Copyright (c) 2007 Rajesh Prabhakaran.
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
 * $FreeBSD: src/sys/dev/mfi/mfi.c,v 1.62 2011/11/09 21:53:49 delphij Exp $
 * FreeBSD projects/head_mfi/ r233016
 */

#include "opt_mfi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/rman.h>
#include <sys/bus_dma.h>
#include <sys/buf2.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/device.h>
#include <sys/mplock2.h>
#include <sys/taskqueue.h>

#include <bus/cam/scsi/scsi_all.h>

#include <bus/pci/pcivar.h>

#include <dev/raid/mfi/mfireg.h>
#include <dev/raid/mfi/mfi_ioctl.h>
#include <dev/raid/mfi/mfivar.h>

static int	mfi_alloc_commands(struct mfi_softc *);
static int	mfi_comms_init(struct mfi_softc *);
static int	mfi_get_controller_info(struct mfi_softc *);
static int	mfi_get_log_state(struct mfi_softc *,
		    struct mfi_evt_log_state **);
static int	mfi_parse_entries(struct mfi_softc *, int, int);
static int	mfi_dcmd_command(struct mfi_softc *, struct mfi_command **,
		    uint32_t, void **, size_t);
static void	mfi_data_cb(void *, bus_dma_segment_t *, int, int);
static void	mfi_startup(void *arg);
static void	mfi_intr(void *arg);
static void	mfi_ldprobe(struct mfi_softc *sc);
static void	mfi_syspdprobe(struct mfi_softc *sc);
static void	mfi_handle_evt(void *context, int pending);
static int	mfi_aen_register(struct mfi_softc *sc, int seq, int locale);
static void	mfi_aen_complete(struct mfi_command *);
static int	mfi_add_ld(struct mfi_softc *sc, int);
static void	mfi_add_ld_complete(struct mfi_command *);
static int	mfi_add_sys_pd(struct mfi_softc *sc, int);
static void	mfi_add_sys_pd_complete(struct mfi_command *);
static struct mfi_command *mfi_bio_command(struct mfi_softc *);
static void	mfi_bio_complete(struct mfi_command *);
static struct mfi_command *mfi_build_ldio(struct mfi_softc *,struct bio*);
static struct mfi_command *mfi_build_syspdio(struct mfi_softc *,struct bio*);
static int	mfi_send_frame(struct mfi_softc *, struct mfi_command *);
static int	mfi_abort(struct mfi_softc *, struct mfi_command *);
static int	mfi_linux_ioctl_int(struct cdev *, u_long, caddr_t, int);
static void	mfi_timeout(void *);
static int	mfi_user_command(struct mfi_softc *,
		    struct mfi_ioc_passthru *);
static void	mfi_enable_intr_xscale(struct mfi_softc *sc);
static void	mfi_enable_intr_ppc(struct mfi_softc *sc);
static int32_t	mfi_read_fw_status_xscale(struct mfi_softc *sc);
static int32_t	mfi_read_fw_status_ppc(struct mfi_softc *sc);
static int	mfi_check_clear_intr_xscale(struct mfi_softc *sc);
static int	mfi_check_clear_intr_ppc(struct mfi_softc *sc);
static void	mfi_issue_cmd_xscale(struct mfi_softc *sc, bus_addr_t bus_add,
		    uint32_t frame_cnt);
static void	mfi_issue_cmd_ppc(struct mfi_softc *sc, bus_addr_t bus_add,
		    uint32_t frame_cnt);
static int mfi_config_lock(struct mfi_softc *sc, uint32_t opcode);
static void mfi_config_unlock(struct mfi_softc *sc, int locked);
static int mfi_check_command_pre(struct mfi_softc *sc, struct mfi_command *cm);
static void mfi_check_command_post(struct mfi_softc *sc, struct mfi_command *cm);
static int mfi_check_for_sscd(struct mfi_softc *sc, struct mfi_command *cm);

static void	mfi_filter_detach(struct knote *);
static int	mfi_filter_read(struct knote *, long);
static int	mfi_filter_write(struct knote *, long);

SYSCTL_NODE(_hw, OID_AUTO, mfi, CTLFLAG_RD, 0, "MFI driver parameters");
static int	mfi_event_locale = MFI_EVT_LOCALE_ALL;
TUNABLE_INT("hw.mfi.event_locale", &mfi_event_locale);
SYSCTL_INT(_hw_mfi, OID_AUTO, event_locale, CTLFLAG_RW, &mfi_event_locale,
            0, "event message locale");

static int	mfi_event_class = MFI_EVT_CLASS_INFO;
TUNABLE_INT("hw.mfi.event_class", &mfi_event_class);
SYSCTL_INT(_hw_mfi, OID_AUTO, event_class, CTLFLAG_RW, &mfi_event_class,
          0, "event message class");

static int	mfi_max_cmds = 128;
TUNABLE_INT("hw.mfi.max_cmds", &mfi_max_cmds);
SYSCTL_INT(_hw_mfi, OID_AUTO, max_cmds, CTLFLAG_RD, &mfi_max_cmds,
	   0, "Max commands");

static int	mfi_detect_jbod_change = 1;
TUNABLE_INT("hw.mfi.detect_jbod_change", &mfi_detect_jbod_change);
SYSCTL_INT(_hw_mfi, OID_AUTO, detect_jbod_change, CTLFLAG_RW,
	   &mfi_detect_jbod_change, 0, "Detect a change to a JBOD");

static int	mfi_cmd_timeout = MFI_CMD_TIMEOUT;
TUNABLE_INT("hw.mfi.cmd_timeout", &mfi_cmd_timeout);
SYSCTL_INT(_hw_mfi, OID_AUTO, cmd_timeout, CTLFLAG_RW, &mfi_cmd_timeout,
	   0, "Command timeout (in seconds)");

/* Management interface */
static d_open_t		mfi_open;
static d_close_t	mfi_close;
static d_ioctl_t	mfi_ioctl;
static d_kqfilter_t	mfi_kqfilter;

static struct dev_ops mfi_ops = {
	{ "mfi", 0, D_MPSAFE },
	.d_open =	mfi_open,
	.d_close =	mfi_close,
	.d_ioctl =	mfi_ioctl,
	.d_kqfilter =	mfi_kqfilter,
};

static struct filterops mfi_read_filterops =
	{ FILTEROP_ISFD, NULL, mfi_filter_detach, mfi_filter_read };
static struct filterops mfi_write_filterops =
	{ FILTEROP_ISFD, NULL, mfi_filter_detach, mfi_filter_write };

MALLOC_DEFINE(M_MFIBUF, "mfibuf", "Buffers for the MFI driver");

#define MFI_INQ_LENGTH SHORT_INQUIRY_LENGTH
struct mfi_skinny_dma_info mfi_skinny;

static void
mfi_enable_intr_xscale(struct mfi_softc *sc)
{
	MFI_WRITE4(sc, MFI_OMSK, 0x01);
}

static void
mfi_enable_intr_ppc(struct mfi_softc *sc)
{
	if (sc->mfi_flags & MFI_FLAGS_1078) {
		MFI_WRITE4(sc, MFI_ODCR0, 0xFFFFFFFF);
		MFI_WRITE4(sc, MFI_OMSK, ~MFI_1078_EIM);
	} else if (sc->mfi_flags & MFI_FLAGS_GEN2) {
		MFI_WRITE4(sc, MFI_ODCR0, 0xFFFFFFFF);
		MFI_WRITE4(sc, MFI_OMSK, ~MFI_GEN2_EIM);
	} else if (sc->mfi_flags & MFI_FLAGS_SKINNY) {
		MFI_WRITE4(sc, MFI_OMSK, ~0x00000001);
	} else {
		panic("unknown adapter type");
	}
}

static int32_t
mfi_read_fw_status_xscale(struct mfi_softc *sc)
{
	return MFI_READ4(sc, MFI_OMSG0);
}

static int32_t
mfi_read_fw_status_ppc(struct mfi_softc *sc)
{
	return MFI_READ4(sc, MFI_OSP0);
}

static int
mfi_check_clear_intr_xscale(struct mfi_softc *sc)
{
	int32_t status;

	status = MFI_READ4(sc, MFI_OSTS);
	if ((status & MFI_OSTS_INTR_VALID) == 0)
		return 1;

	MFI_WRITE4(sc, MFI_OSTS, status);
	return 0;
}

static int
mfi_check_clear_intr_ppc(struct mfi_softc *sc)
{
	int32_t status;

	status = MFI_READ4(sc, MFI_OSTS);
	if (((sc->mfi_flags & MFI_FLAGS_1078) && !(status & MFI_1078_RM)) ||
	    ((sc->mfi_flags & MFI_FLAGS_GEN2) && !(status & MFI_GEN2_RM)) ||
	    ((sc->mfi_flags & MFI_FLAGS_SKINNY) && !(status & MFI_SKINNY_RM)))
		return 1;

	if (sc->mfi_flags & MFI_FLAGS_SKINNY)
		MFI_WRITE4(sc, MFI_OSTS, status);
	else
		MFI_WRITE4(sc, MFI_ODCR0, status);
	return 0;
}

static void
mfi_issue_cmd_xscale(struct mfi_softc *sc, bus_addr_t bus_add, uint32_t frame_cnt)
{
	MFI_WRITE4(sc, MFI_IQP,(bus_add >>3) | frame_cnt);
}

static void
mfi_issue_cmd_ppc(struct mfi_softc *sc, bus_addr_t bus_add, uint32_t frame_cnt)
{
	if (sc->mfi_flags & MFI_FLAGS_SKINNY) {
		MFI_WRITE4(sc, MFI_IQPL, (bus_add | frame_cnt << 1) | 1);
		MFI_WRITE4(sc, MFI_IQPH, 0x00000000);
	} else {
		MFI_WRITE4(sc, MFI_IQP, (bus_add | frame_cnt << 1) | 1);
	}
}

int
mfi_transition_firmware(struct mfi_softc *sc)
{
	uint32_t fw_state, cur_state;
	int max_wait, i;
	uint32_t cur_abs_reg_val = 0;
	uint32_t prev_abs_reg_val = 0;

	cur_abs_reg_val = sc->mfi_read_fw_status(sc);
	fw_state = cur_abs_reg_val & MFI_FWSTATE_MASK;
	while (fw_state != MFI_FWSTATE_READY) {
		if (bootverbose)
			device_printf(sc->mfi_dev, "Waiting for firmware to "
			"become ready\n");
		cur_state = fw_state;
		switch (fw_state) {
		case MFI_FWSTATE_FAULT:
			device_printf(sc->mfi_dev, "Firmware fault\n");
			return (ENXIO);
		case MFI_FWSTATE_WAIT_HANDSHAKE:
			if (sc->mfi_flags & MFI_FLAGS_SKINNY || sc->mfi_flags & MFI_FLAGS_TBOLT)
			    MFI_WRITE4(sc, MFI_SKINNY_IDB, MFI_FWINIT_CLEAR_HANDSHAKE);
			else
			    MFI_WRITE4(sc, MFI_IDB, MFI_FWINIT_CLEAR_HANDSHAKE);
			max_wait = MFI_RESET_WAIT_TIME;
			break;
		case MFI_FWSTATE_OPERATIONAL:
			if (sc->mfi_flags & MFI_FLAGS_SKINNY || sc->mfi_flags & MFI_FLAGS_TBOLT)
			    MFI_WRITE4(sc, MFI_SKINNY_IDB, 7);
			else
			    MFI_WRITE4(sc, MFI_IDB, MFI_FWINIT_READY);
			max_wait = MFI_RESET_WAIT_TIME;
			break;
		case MFI_FWSTATE_UNDEFINED:
		case MFI_FWSTATE_BB_INIT:
			max_wait = MFI_RESET_WAIT_TIME;
			break;
		case MFI_FWSTATE_FW_INIT_2:
			max_wait = MFI_RESET_WAIT_TIME;
			break;
		case MFI_FWSTATE_FW_INIT:
		case MFI_FWSTATE_FLUSH_CACHE:
			max_wait = MFI_RESET_WAIT_TIME;
			break;
		case MFI_FWSTATE_DEVICE_SCAN:
			max_wait = MFI_RESET_WAIT_TIME; /* wait for 180 seconds */
			prev_abs_reg_val = cur_abs_reg_val;
			break;
		case MFI_FWSTATE_BOOT_MESSAGE_PENDING:
			if (sc->mfi_flags & MFI_FLAGS_SKINNY || sc->mfi_flags & MFI_FLAGS_TBOLT)
			    MFI_WRITE4(sc, MFI_SKINNY_IDB, MFI_FWINIT_HOTPLUG);
			else
			    MFI_WRITE4(sc, MFI_IDB, MFI_FWINIT_HOTPLUG);
			max_wait = MFI_RESET_WAIT_TIME;
			break;
		default:
			device_printf(sc->mfi_dev, "Unknown firmware state %#x\n",
			    fw_state);
			return (ENXIO);
		}
		for (i = 0; i < (max_wait * 10); i++) {
			cur_abs_reg_val = sc->mfi_read_fw_status(sc);
			fw_state = cur_abs_reg_val & MFI_FWSTATE_MASK;
			if (fw_state == cur_state)
				DELAY(100000);
			else
				break;
		}
		if (fw_state == MFI_FWSTATE_DEVICE_SCAN) {
			/* Check the device scanning progress */
			if (prev_abs_reg_val != cur_abs_reg_val)
				continue;
		}
		if (fw_state == cur_state) {
			device_printf(sc->mfi_dev, "Firmware stuck in state "
			    "%#x\n", fw_state);
			return (ENXIO);
		}
	}
	return (0);
}

static void
mfi_addr_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	bus_addr_t *addr;

	addr = arg;
	*addr = segs[0].ds_addr;
}

int
mfi_attach(struct mfi_softc *sc)
{
	uint32_t status;
	int error, commsz, framessz, sensesz;
	int frames, unit, max_fw_sge;
	uint32_t tb_mem_size = 0;

	if (sc == NULL)
		return EINVAL;

	device_printf(sc->mfi_dev, "Megaraid SAS driver Ver %s \n",
	    MEGASAS_VERSION);

	lockinit(&sc->mfi_io_lock, "MFI I/O lock", 0, LK_CANRECURSE);
	lockinit(&sc->mfi_config_lock, "MFI config", 0, LK_CANRECURSE);
	TAILQ_INIT(&sc->mfi_ld_tqh);
	TAILQ_INIT(&sc->mfi_syspd_tqh);
	TAILQ_INIT(&sc->mfi_evt_queue);
	TASK_INIT(&sc->mfi_evt_task, 0, mfi_handle_evt, sc);
	TAILQ_INIT(&sc->mfi_aen_pids);
	TAILQ_INIT(&sc->mfi_cam_ccbq);

	mfi_initq_free(sc);
	mfi_initq_ready(sc);
	mfi_initq_busy(sc);
	mfi_initq_bio(sc);

	sc->adpreset = 0;
	sc->last_seq_num = 0;
	sc->disableOnlineCtrlReset = 1;
	sc->issuepend_done = 1;
	sc->hw_crit_error = 0;

	if (sc->mfi_flags & MFI_FLAGS_1064R) {
		sc->mfi_enable_intr = mfi_enable_intr_xscale;
		sc->mfi_read_fw_status = mfi_read_fw_status_xscale;
		sc->mfi_check_clear_intr = mfi_check_clear_intr_xscale;
		sc->mfi_issue_cmd = mfi_issue_cmd_xscale;
	} else if (sc->mfi_flags & MFI_FLAGS_TBOLT) {
		sc->mfi_enable_intr = mfi_tbolt_enable_intr_ppc;
		sc->mfi_disable_intr = mfi_tbolt_disable_intr_ppc;
		sc->mfi_read_fw_status = mfi_tbolt_read_fw_status_ppc;
		sc->mfi_check_clear_intr = mfi_tbolt_check_clear_intr_ppc;
		sc->mfi_issue_cmd = mfi_tbolt_issue_cmd_ppc;
		sc->mfi_adp_reset = mfi_tbolt_adp_reset;
		sc->mfi_tbolt = 1;
		TAILQ_INIT(&sc->mfi_cmd_tbolt_tqh);
	} else {
		sc->mfi_enable_intr =  mfi_enable_intr_ppc;
		sc->mfi_read_fw_status = mfi_read_fw_status_ppc;
		sc->mfi_check_clear_intr = mfi_check_clear_intr_ppc;
		sc->mfi_issue_cmd = mfi_issue_cmd_ppc;
	}


	/* Before we get too far, see if the firmware is working */
	if ((error = mfi_transition_firmware(sc)) != 0) {
		device_printf(sc->mfi_dev, "Firmware not in READY state, "
		    "error %d\n", error);
		return (ENXIO);
	}

	/* Start: LSIP200113393 */
	if (bus_dma_tag_create( sc->mfi_parent_dmat,	/* parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				MEGASAS_MAX_NAME*sizeof(bus_addr_t),			/* maxsize */
				1,			/* msegments */
				MEGASAS_MAX_NAME*sizeof(bus_addr_t),			/* maxsegsize */
				0,			/* flags */
				&sc->verbuf_h_dmat)) {
		device_printf(sc->mfi_dev, "Cannot allocate verbuf_h_dmat DMA tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->verbuf_h_dmat, (void **)&sc->verbuf,
	    BUS_DMA_NOWAIT, &sc->verbuf_h_dmamap)) {
		device_printf(sc->mfi_dev, "Cannot allocate verbuf_h_dmamap memory\n");
		return (ENOMEM);
	}
	bzero(sc->verbuf, MEGASAS_MAX_NAME*sizeof(bus_addr_t));
	bus_dmamap_load(sc->verbuf_h_dmat, sc->verbuf_h_dmamap,
	    sc->verbuf, MEGASAS_MAX_NAME*sizeof(bus_addr_t),
	    mfi_addr_cb, &sc->verbuf_h_busaddr, 0);
	/* End: LSIP200113393 */

	/*
	 * Get information needed for sizing the contiguous memory for the
	 * frame pool.  Size down the sgl parameter since we know that
	 * we will never need more than what's required for MAXPHYS.
	 * It would be nice if these constants were available at runtime
	 * instead of compile time.
	 */
	status = sc->mfi_read_fw_status(sc);
	sc->mfi_max_fw_cmds = status & MFI_FWSTATE_MAXCMD_MASK;
	max_fw_sge = (status & MFI_FWSTATE_MAXSGL_MASK) >> 16;
	sc->mfi_max_sge = min(max_fw_sge, ((MFI_MAXPHYS / PAGE_SIZE) + 1));

	/* ThunderBolt Support get the contiguous memory */

	if (sc->mfi_flags & MFI_FLAGS_TBOLT) {
		mfi_tbolt_init_globals(sc);
		device_printf(sc->mfi_dev, "MaxCmd = %x MaxSgl = %x state = %x \n",
		    sc->mfi_max_fw_cmds, sc->mfi_max_sge, status);
		tb_mem_size = mfi_tbolt_get_memory_requirement(sc);

		if (bus_dma_tag_create( sc->mfi_parent_dmat,	/* parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				tb_mem_size,		/* maxsize */
				1,			/* msegments */
				tb_mem_size,		/* maxsegsize */
				0,			/* flags */
				&sc->mfi_tb_dmat)) {
			device_printf(sc->mfi_dev, "Cannot allocate comms DMA tag\n");
			return (ENOMEM);
		}
		if (bus_dmamem_alloc(sc->mfi_tb_dmat, (void **)&sc->request_message_pool,
		BUS_DMA_NOWAIT, &sc->mfi_tb_dmamap)) {
			device_printf(sc->mfi_dev, "Cannot allocate comms memory\n");
			return (ENOMEM);
		}
		bzero(sc->request_message_pool, tb_mem_size);
		bus_dmamap_load(sc->mfi_tb_dmat, sc->mfi_tb_dmamap,
		sc->request_message_pool, tb_mem_size, mfi_addr_cb, &sc->mfi_tb_busaddr, 0);

		/* For ThunderBolt memory init */
		if (bus_dma_tag_create( sc->mfi_parent_dmat,	/* parent */
				0x100, 0,		/* alignmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				MFI_FRAME_SIZE,		/* maxsize */
				1,			/* msegments */
				MFI_FRAME_SIZE,		/* maxsegsize */
				0,			/* flags */
				&sc->mfi_tb_init_dmat)) {
		device_printf(sc->mfi_dev, "Cannot allocate init DMA tag\n");
		return (ENOMEM);
		}
		if (bus_dmamem_alloc(sc->mfi_tb_init_dmat, (void **)&sc->mfi_tb_init,
		    BUS_DMA_NOWAIT, &sc->mfi_tb_init_dmamap)) {
			device_printf(sc->mfi_dev, "Cannot allocate init memory\n");
			return (ENOMEM);
		}
		bzero(sc->mfi_tb_init, MFI_FRAME_SIZE);
		bus_dmamap_load(sc->mfi_tb_init_dmat, sc->mfi_tb_init_dmamap,
		sc->mfi_tb_init, MFI_FRAME_SIZE, mfi_addr_cb,
		    &sc->mfi_tb_init_busaddr, 0);
		if (mfi_tbolt_init_desc_pool(sc, sc->request_message_pool,
		    tb_mem_size)) {
			device_printf(sc->mfi_dev,
			    "Thunderbolt pool preparation error\n");
			return 0;
		}

		/*
		  Allocate DMA memory mapping for MPI2 IOC Init descriptor,
		  we are taking it diffrent from what we have allocated for Request
		  and reply descriptors to avoid confusion later
		*/
		tb_mem_size = sizeof(struct MPI2_IOC_INIT_REQUEST);
		if (bus_dma_tag_create( sc->mfi_parent_dmat,	/* parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				tb_mem_size,		/* maxsize */
				1,			/* msegments */
				tb_mem_size,		/* maxsegsize */
				0,			/* flags */
				&sc->mfi_tb_ioc_init_dmat)) {
			device_printf(sc->mfi_dev,
			    "Cannot allocate comms DMA tag\n");
			return (ENOMEM);
		}
		if (bus_dmamem_alloc(sc->mfi_tb_ioc_init_dmat,
		    (void **)&sc->mfi_tb_ioc_init_desc,
		    BUS_DMA_NOWAIT, &sc->mfi_tb_ioc_init_dmamap)) {
			device_printf(sc->mfi_dev, "Cannot allocate comms memory\n");
			return (ENOMEM);
		}
		bzero(sc->mfi_tb_ioc_init_desc, tb_mem_size);
		bus_dmamap_load(sc->mfi_tb_ioc_init_dmat, sc->mfi_tb_ioc_init_dmamap,
		sc->mfi_tb_ioc_init_desc, tb_mem_size, mfi_addr_cb,
		    &sc->mfi_tb_ioc_init_busaddr, 0);
	}
	/*
	 * Create the dma tag for data buffers.  Used both for block I/O
	 * and for various internal data queries.
	 */
	if (bus_dma_tag_create( sc->mfi_parent_dmat,	/* parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR,	/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
				sc->mfi_max_sge,	/* nsegments */
				BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
				BUS_DMA_ALLOCNOW,	/* flags */
				&sc->mfi_buffer_dmat)) {
		device_printf(sc->mfi_dev, "Cannot allocate buffer DMA tag\n");
		return (ENOMEM);
	}

	/*
	 * Allocate DMA memory for the comms queues.  Keep it under 4GB for
	 * efficiency.  The mfi_hwcomms struct includes space for 1 reply queue
	 * entry, so the calculated size here will be will be 1 more than
	 * mfi_max_fw_cmds.  This is apparently a requirement of the hardware.
	 */
	commsz = (sizeof(uint32_t) * sc->mfi_max_fw_cmds) +
	    sizeof(struct mfi_hwcomms);
	if (bus_dma_tag_create( sc->mfi_parent_dmat,	/* parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				commsz,			/* maxsize */
				1,			/* msegments */
				commsz,			/* maxsegsize */
				0,			/* flags */
				&sc->mfi_comms_dmat)) {
		device_printf(sc->mfi_dev, "Cannot allocate comms DMA tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->mfi_comms_dmat, (void **)&sc->mfi_comms,
	    BUS_DMA_NOWAIT, &sc->mfi_comms_dmamap)) {
		device_printf(sc->mfi_dev, "Cannot allocate comms memory\n");
		return (ENOMEM);
	}
	bzero(sc->mfi_comms, commsz);
	bus_dmamap_load(sc->mfi_comms_dmat, sc->mfi_comms_dmamap,
	    sc->mfi_comms, commsz, mfi_addr_cb, &sc->mfi_comms_busaddr, 0);
	/*
	 * Allocate DMA memory for the command frames.  Keep them in the
	 * lower 4GB for efficiency.  Calculate the size of the commands at
	 * the same time; each command is one 64 byte frame plus a set of
         * additional frames for holding sg lists or other data.
	 * The assumption here is that the SG list will start at the second
	 * frame and not use the unused bytes in the first frame.  While this
	 * isn't technically correct, it simplifies the calculation and allows
	 * for command frames that might be larger than an mfi_io_frame.
	 */
	if (sizeof(bus_addr_t) == 8) {
		sc->mfi_sge_size = sizeof(struct mfi_sg64);
		sc->mfi_flags |= MFI_FLAGS_SG64;
	} else {
		sc->mfi_sge_size = sizeof(struct mfi_sg32);
	}
	if (sc->mfi_flags & MFI_FLAGS_SKINNY)
		sc->mfi_sge_size = sizeof(struct mfi_sg_skinny);
	frames = (sc->mfi_sge_size * sc->mfi_max_sge - 1) / MFI_FRAME_SIZE + 2;
	sc->mfi_cmd_size = frames * MFI_FRAME_SIZE;
	framessz = sc->mfi_cmd_size * sc->mfi_max_fw_cmds;
	if (bus_dma_tag_create( sc->mfi_parent_dmat,	/* parent */
				64, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				framessz,		/* maxsize */
				1,			/* nsegments */
				framessz,		/* maxsegsize */
				0,			/* flags */
				&sc->mfi_frames_dmat)) {
		device_printf(sc->mfi_dev, "Cannot allocate frame DMA tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->mfi_frames_dmat, (void **)&sc->mfi_frames,
	    BUS_DMA_NOWAIT, &sc->mfi_frames_dmamap)) {
		device_printf(sc->mfi_dev, "Cannot allocate frames memory\n");
		return (ENOMEM);
	}
	bzero(sc->mfi_frames, framessz);
	bus_dmamap_load(sc->mfi_frames_dmat, sc->mfi_frames_dmamap,
	    sc->mfi_frames, framessz, mfi_addr_cb, &sc->mfi_frames_busaddr,0);
	/*
	 * Allocate DMA memory for the frame sense data.  Keep them in the
	 * lower 4GB for efficiency
	 */
	sensesz = sc->mfi_max_fw_cmds * MFI_SENSE_LEN;
	if (bus_dma_tag_create( sc->mfi_parent_dmat,	/* parent */
				4, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				sensesz,		/* maxsize */
				1,			/* nsegments */
				sensesz,		/* maxsegsize */
				0,			/* flags */
				&sc->mfi_sense_dmat)) {
		device_printf(sc->mfi_dev, "Cannot allocate sense DMA tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->mfi_sense_dmat, (void **)&sc->mfi_sense,
	    BUS_DMA_NOWAIT, &sc->mfi_sense_dmamap)) {
		device_printf(sc->mfi_dev, "Cannot allocate sense memory\n");
		return (ENOMEM);
	}
	bus_dmamap_load(sc->mfi_sense_dmat, sc->mfi_sense_dmamap,
	    sc->mfi_sense, sensesz, mfi_addr_cb, &sc->mfi_sense_busaddr, 0);
	if ((error = mfi_alloc_commands(sc)) != 0)
		return (error);

	/*
	 * Before moving the FW to operational state, check whether
	 * hostmemory is required by the FW or not
	 */

	/* ThunderBolt MFI_IOC2 INIT */
	if (sc->mfi_flags & MFI_FLAGS_TBOLT) {
		sc->mfi_disable_intr(sc);
		if ((error = mfi_tbolt_init_MFI_queue(sc)) != 0) {
			device_printf(sc->mfi_dev,
			    "TB Init has failed with error %d\n",error);
			return error;
		}

		if ((error = mfi_tbolt_alloc_cmd(sc)) != 0)
			return error;
		if (bus_setup_intr(sc->mfi_dev, sc->mfi_irq, INTR_MPSAFE,
			mfi_intr_tbolt, sc, &sc->mfi_intr, NULL)) {
			device_printf(sc->mfi_dev, "Cannot set up interrupt\n");
			return (EINVAL);
		}
		sc->mfi_enable_intr(sc);
		sc->map_id = 0;
	} else {
		if ((error = mfi_comms_init(sc)) != 0)
			return (error);

		if (bus_setup_intr(sc->mfi_dev, sc->mfi_irq, INTR_MPSAFE,
			mfi_intr, sc, &sc->mfi_intr, NULL)) {
			device_printf(sc->mfi_dev, "Cannot set up interrupt\n");
			return (EINVAL);
		}
		sc->mfi_enable_intr(sc);
	}
	if ((error = mfi_get_controller_info(sc)) != 0)
		return (error);
	sc->disableOnlineCtrlReset = 0;

	/* Register a config hook to probe the bus for arrays */
	sc->mfi_ich.ich_func = mfi_startup;
	sc->mfi_ich.ich_arg = sc;
	sc->mfi_ich.ich_desc = "mfi";
	if (config_intrhook_establish(&sc->mfi_ich) != 0) {
		device_printf(sc->mfi_dev, "Cannot establish configuration "
		    "hook\n");
		return (EINVAL);
	}
	if ((error = mfi_aen_setup(sc, 0), 0) != 0) {
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		return (error);
	}

	/*
	 * Register a shutdown handler.
	 */
	if ((sc->mfi_eh = EVENTHANDLER_REGISTER(shutdown_final, mfi_shutdown,
	    sc, SHUTDOWN_PRI_DEFAULT)) == NULL) {
		device_printf(sc->mfi_dev, "Warning: shutdown event "
		    "registration failed\n");
	}

	/*
	 * Create the control device for doing management
	 */
	unit = device_get_unit(sc->mfi_dev);
	sc->mfi_cdev = make_dev(&mfi_ops, unit, UID_ROOT, GID_OPERATOR,
	    0640, "mfi%d", unit);
	if (unit == 0)
		make_dev_alias(sc->mfi_cdev, "megaraid_sas_ioctl_node");
	if (sc->mfi_cdev != NULL)
		sc->mfi_cdev->si_drv1 = sc;
	SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->mfi_dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->mfi_dev)),
	    OID_AUTO, "delete_busy_volumes", CTLFLAG_RW,
	    &sc->mfi_delete_busy_volumes, 0, "Allow removal of busy volumes");
	SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->mfi_dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->mfi_dev)),
	    OID_AUTO, "keep_deleted_volumes", CTLFLAG_RW,
	    &sc->mfi_keep_deleted_volumes, 0,
	    "Don't detach the mfid device for a busy volume that is deleted");

	device_add_child(sc->mfi_dev, "mfip", -1);
	bus_generic_attach(sc->mfi_dev);

	/* Start the timeout watchdog */
	callout_init_mp(&sc->mfi_watchdog_callout);
	callout_reset(&sc->mfi_watchdog_callout, mfi_cmd_timeout * hz,
	    mfi_timeout, sc);

	return (0);
}

static int
mfi_alloc_commands(struct mfi_softc *sc)
{
	struct mfi_command *cm;
	int i, ncmds;

	/*
	 * XXX Should we allocate all the commands up front, or allocate on
	 * demand later like 'aac' does?
	 */
	ncmds = MIN(mfi_max_cmds, sc->mfi_max_fw_cmds);
	if (bootverbose)
		device_printf(sc->mfi_dev, "Max fw cmds= %d, sizing driver "
		   "pool to %d\n", sc->mfi_max_fw_cmds, ncmds);

	sc->mfi_commands = kmalloc(sizeof(struct mfi_command) * ncmds, M_MFIBUF,
	    M_WAITOK | M_ZERO);

	for (i = 0; i < ncmds; i++) {
		cm = &sc->mfi_commands[i];
		cm->cm_frame = (union mfi_frame *)((uintptr_t)sc->mfi_frames +
		    sc->mfi_cmd_size * i);
		cm->cm_frame_busaddr = sc->mfi_frames_busaddr +
		    sc->mfi_cmd_size * i;
		cm->cm_frame->header.context = i;
		cm->cm_sense = &sc->mfi_sense[i];
		cm->cm_sense_busaddr= sc->mfi_sense_busaddr + MFI_SENSE_LEN * i;
		cm->cm_sc = sc;
		cm->cm_index = i;
		if (bus_dmamap_create(sc->mfi_buffer_dmat, 0,
		    &cm->cm_dmamap) == 0) {
			lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
			mfi_release_command(cm);
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		}
		else
			break;
		sc->mfi_total_cmds++;
	}

	return (0);
}

void
mfi_release_command(struct mfi_command *cm)
{
	struct mfi_frame_header *hdr;
	uint32_t *hdr_data;

	mfi_lockassert(&cm->cm_sc->mfi_io_lock);

	/*
	 * Zero out the important fields of the frame, but make sure the
	 * context field is preserved.  For efficiency, handle the fields
	 * as 32 bit words.  Clear out the first S/G entry too for safety.
	 */
	hdr = &cm->cm_frame->header;
	if (cm->cm_data != NULL && hdr->sg_count) {
		cm->cm_sg->sg32[0].len = 0;
		cm->cm_sg->sg32[0].addr = 0;
	}

	hdr_data = (uint32_t *)cm->cm_frame;
	hdr_data[0] = 0;	/* cmd, sense_len, cmd_status, scsi_status */
	hdr_data[1] = 0;	/* target_id, lun_id, cdb_len, sg_count */
	hdr_data[4] = 0;	/* flags, timeout */
	hdr_data[5] = 0;	/* data_len */

	cm->cm_extra_frames = 0;
	cm->cm_flags = 0;
	cm->cm_complete = NULL;
	cm->cm_private = NULL;
	cm->cm_data = NULL;
	cm->cm_sg = 0;
	cm->cm_total_frame_size = 0;
	cm->retry_for_fw_reset = 0;

	mfi_enqueue_free(cm);
}

static int
mfi_dcmd_command(struct mfi_softc *sc, struct mfi_command **cmp,
    uint32_t opcode, void **bufp, size_t bufsize)
{
	struct mfi_command *cm;
	struct mfi_dcmd_frame *dcmd;
	void *buf = NULL;
	uint32_t context = 0;

	mfi_lockassert(&sc->mfi_io_lock);

	cm = mfi_dequeue_free(sc);
	if (cm == NULL)
		return (EBUSY);

	/* Zero out the MFI frame */
	context = cm->cm_frame->header.context;
	bzero(cm->cm_frame, sizeof(union mfi_frame));
	cm->cm_frame->header.context = context;

	if ((bufsize > 0) && (bufp != NULL)) {
		if (*bufp == NULL) {
			buf = kmalloc(bufsize, M_MFIBUF, M_NOWAIT|M_ZERO);
			if (buf == NULL) {
				mfi_release_command(cm);
				return (ENOMEM);
			}
			*bufp = buf;
		} else {
			buf = *bufp;
		}
	}

	dcmd =  &cm->cm_frame->dcmd;
	bzero(dcmd->mbox, MFI_MBOX_SIZE);
	dcmd->header.cmd = MFI_CMD_DCMD;
	dcmd->header.timeout = 0;
	dcmd->header.flags = 0;
	dcmd->header.data_len = bufsize;
	dcmd->header.scsi_status = 0;
	dcmd->opcode = opcode;
	cm->cm_sg = &dcmd->sgl;
	cm->cm_total_frame_size = MFI_DCMD_FRAME_SIZE;
	cm->cm_flags = 0;
	cm->cm_data = buf;
	cm->cm_private = buf;
	cm->cm_len = bufsize;

	*cmp = cm;
	if ((bufp != NULL) && (*bufp == NULL) && (buf != NULL))
		*bufp = buf;
	return (0);
}

static int
mfi_comms_init(struct mfi_softc *sc)
{
	struct mfi_command *cm;
	struct mfi_init_frame *init;
	struct mfi_init_qinfo *qinfo;
	int error;
	uint32_t context = 0;

	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	if ((cm = mfi_dequeue_free(sc)) == NULL) {
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		return (EBUSY);
	}

	/* Zero out the MFI frame */
	context = cm->cm_frame->header.context;
	bzero(cm->cm_frame, sizeof(union mfi_frame));
	cm->cm_frame->header.context = context;

	/*
	 * Abuse the SG list area of the frame to hold the init_qinfo
	 * object;
	 */
	init = &cm->cm_frame->init;
	qinfo = (struct mfi_init_qinfo *)((uintptr_t)init + MFI_FRAME_SIZE);

	bzero(qinfo, sizeof(struct mfi_init_qinfo));
	qinfo->rq_entries = sc->mfi_max_fw_cmds + 1;
	qinfo->rq_addr_lo = sc->mfi_comms_busaddr +
	    offsetof(struct mfi_hwcomms, hw_reply_q);
	qinfo->pi_addr_lo = sc->mfi_comms_busaddr +
	    offsetof(struct mfi_hwcomms, hw_pi);
	qinfo->ci_addr_lo = sc->mfi_comms_busaddr +
	    offsetof(struct mfi_hwcomms, hw_ci);

	init->header.cmd = MFI_CMD_INIT;
	init->header.data_len = sizeof(struct mfi_init_qinfo);
	init->qinfo_new_addr_lo = cm->cm_frame_busaddr + MFI_FRAME_SIZE;
	cm->cm_data = NULL;
	cm->cm_flags = MFI_CMD_POLLED;

	if ((error = mfi_mapcmd(sc, cm)) != 0) {
		device_printf(sc->mfi_dev, "failed to send init command\n");
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		return (error);
	}
	mfi_release_command(cm);
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);

	return (0);
}

static int
mfi_get_controller_info(struct mfi_softc *sc)
{
	struct mfi_command *cm = NULL;
	struct mfi_ctrl_info *ci = NULL;
	uint32_t max_sectors_1, max_sectors_2;
	int error;

	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	error = mfi_dcmd_command(sc, &cm, MFI_DCMD_CTRL_GETINFO,
	    (void **)&ci, sizeof(*ci));
	if (error)
		goto out;
	cm->cm_flags = MFI_CMD_DATAIN | MFI_CMD_POLLED;

	if ((error = mfi_mapcmd(sc, cm)) != 0) {
		device_printf(sc->mfi_dev, "Failed to get controller info\n");
		sc->mfi_max_io = (sc->mfi_max_sge - 1) * PAGE_SIZE /
		    MFI_SECTOR_LEN;
		error = 0;
		goto out;
	}

	bus_dmamap_sync(sc->mfi_buffer_dmat, cm->cm_dmamap,
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->mfi_buffer_dmat, cm->cm_dmamap);

	max_sectors_1 = (1 << ci->stripe_sz_ops.max) * ci->max_strips_per_io;
	max_sectors_2 = ci->max_request_size;
	sc->mfi_max_io = min(max_sectors_1, max_sectors_2);
	sc->disableOnlineCtrlReset =
	    ci->properties.OnOffProperties.disableOnlineCtrlReset;

out:
	if (ci)
		kfree(ci, M_MFIBUF);
	if (cm)
		mfi_release_command(cm);
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);
	return (error);
}

static int
mfi_get_log_state(struct mfi_softc *sc, struct mfi_evt_log_state **log_state)
{
	struct mfi_command *cm = NULL;
	int error;

	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	error = mfi_dcmd_command(sc, &cm, MFI_DCMD_CTRL_EVENT_GETINFO,
	    (void **)log_state, sizeof(**log_state));
	if (error)
		goto out;
	cm->cm_flags = MFI_CMD_DATAIN | MFI_CMD_POLLED;

	if ((error = mfi_mapcmd(sc, cm)) != 0) {
		device_printf(sc->mfi_dev, "Failed to get log state\n");
		goto out;
	}

	bus_dmamap_sync(sc->mfi_buffer_dmat, cm->cm_dmamap,
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->mfi_buffer_dmat, cm->cm_dmamap);

out:
	if (cm)
		mfi_release_command(cm);
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);

	return (error);
}

int
mfi_aen_setup(struct mfi_softc *sc, uint32_t seq_start)
{
	struct mfi_evt_log_state *log_state = NULL;
	union mfi_evt class_locale;
	int error = 0;
	uint32_t seq;

	class_locale.members.reserved = 0;
	class_locale.members.locale = mfi_event_locale;
	class_locale.members.evt_class  = mfi_event_class;

	if (seq_start == 0) {
		error = mfi_get_log_state(sc, &log_state);
		sc->mfi_boot_seq_num = log_state->boot_seq_num;
		if (error) {
			if (log_state)
				kfree(log_state, M_MFIBUF);
			return (error);
		}

		/*
		 * Walk through any events that fired since the last
		 * shutdown.
		 */
		mfi_parse_entries(sc, log_state->shutdown_seq_num,
		    log_state->newest_seq_num);
		seq = log_state->newest_seq_num;
	} else
		seq = seq_start;
	mfi_aen_register(sc, seq, class_locale.word);
	if (log_state != NULL)
		kfree(log_state, M_MFIBUF);

	return 0;
}

int
mfi_wait_command(struct mfi_softc *sc, struct mfi_command *cm)
{

	mfi_lockassert(&sc->mfi_io_lock);
	cm->cm_complete = NULL;


	/*
	 * MegaCli can issue a DCMD of 0.  In this case do nothing
	 * and return 0 to it as status
	 */
	if (cm->cm_frame->dcmd.opcode == 0) {
		cm->cm_frame->header.cmd_status = MFI_STAT_OK;
		cm->cm_error = 0;
		return (cm->cm_error);
	}
	mfi_enqueue_ready(cm);
	mfi_startio(sc);
	if ((cm->cm_flags & MFI_CMD_COMPLETED) == 0)
		lksleep(cm, &sc->mfi_io_lock, 0, "mfiwait", 0);
	return (cm->cm_error);
}

void
mfi_free(struct mfi_softc *sc)
{
	struct mfi_command *cm;
	int i;

	callout_terminate(&sc->mfi_watchdog_callout);

	if (sc->mfi_cdev != NULL)
		destroy_dev(sc->mfi_cdev);
	dev_ops_remove_minor(&mfi_ops, device_get_unit(sc->mfi_dev));

	if (sc->mfi_total_cmds != 0) {
		for (i = 0; i < sc->mfi_total_cmds; i++) {
			cm = &sc->mfi_commands[i];
			bus_dmamap_destroy(sc->mfi_buffer_dmat, cm->cm_dmamap);
		}
		kfree(sc->mfi_commands, M_MFIBUF);
	}

	if (sc->mfi_intr)
		bus_teardown_intr(sc->mfi_dev, sc->mfi_irq, sc->mfi_intr);
	if (sc->mfi_irq != NULL)
		bus_release_resource(sc->mfi_dev, SYS_RES_IRQ, sc->mfi_irq_rid,
		    sc->mfi_irq);

	if (sc->mfi_sense_busaddr != 0)
		bus_dmamap_unload(sc->mfi_sense_dmat, sc->mfi_sense_dmamap);
	if (sc->mfi_sense != NULL)
		bus_dmamem_free(sc->mfi_sense_dmat, sc->mfi_sense,
		    sc->mfi_sense_dmamap);
	if (sc->mfi_sense_dmat != NULL)
		bus_dma_tag_destroy(sc->mfi_sense_dmat);

	if (sc->mfi_frames_busaddr != 0)
		bus_dmamap_unload(sc->mfi_frames_dmat, sc->mfi_frames_dmamap);
	if (sc->mfi_frames != NULL)
		bus_dmamem_free(sc->mfi_frames_dmat, sc->mfi_frames,
		    sc->mfi_frames_dmamap);
	if (sc->mfi_frames_dmat != NULL)
		bus_dma_tag_destroy(sc->mfi_frames_dmat);

	if (sc->mfi_comms_busaddr != 0)
		bus_dmamap_unload(sc->mfi_comms_dmat, sc->mfi_comms_dmamap);
	if (sc->mfi_comms != NULL)
		bus_dmamem_free(sc->mfi_comms_dmat, sc->mfi_comms,
		    sc->mfi_comms_dmamap);
	if (sc->mfi_comms_dmat != NULL)
		bus_dma_tag_destroy(sc->mfi_comms_dmat);

	/* ThunderBolt contiguous memory free here */
	if (sc->mfi_flags & MFI_FLAGS_TBOLT) {
		if (sc->mfi_tb_busaddr != 0)
			bus_dmamap_unload(sc->mfi_tb_dmat, sc->mfi_tb_dmamap);
		if (sc->request_message_pool != NULL)
			bus_dmamem_free(sc->mfi_tb_dmat, sc->request_message_pool,
			    sc->mfi_tb_dmamap);
		if (sc->mfi_tb_dmat != NULL)
			bus_dma_tag_destroy(sc->mfi_tb_dmat);

		/* Version buffer memory free */
		/* Start LSIP200113393 */
		if (sc->verbuf_h_busaddr != 0)
			bus_dmamap_unload(sc->verbuf_h_dmat, sc->verbuf_h_dmamap);
		if (sc->verbuf != NULL)
			bus_dmamem_free(sc->verbuf_h_dmat, sc->verbuf,
			    sc->verbuf_h_dmamap);
		if (sc->verbuf_h_dmat != NULL)
			bus_dma_tag_destroy(sc->verbuf_h_dmat);

		/* End LSIP200113393 */
		/* ThunderBolt INIT packet memory Free */
		if (sc->mfi_tb_init_busaddr != 0)
			bus_dmamap_unload(sc->mfi_tb_init_dmat, sc->mfi_tb_init_dmamap);
		if (sc->mfi_tb_init != NULL)
			bus_dmamem_free(sc->mfi_tb_init_dmat, sc->mfi_tb_init,
			    sc->mfi_tb_init_dmamap);
		if (sc->mfi_tb_init_dmat != NULL)
			bus_dma_tag_destroy(sc->mfi_tb_init_dmat);

		/* ThunderBolt IOC Init Desc memory free here */
		if (sc->mfi_tb_ioc_init_busaddr != 0)
			bus_dmamap_unload(sc->mfi_tb_ioc_init_dmat,
			    sc->mfi_tb_ioc_init_dmamap);
		if (sc->mfi_tb_ioc_init_desc != NULL)
			bus_dmamem_free(sc->mfi_tb_ioc_init_dmat,
			    sc->mfi_tb_ioc_init_desc,
			    sc->mfi_tb_ioc_init_dmamap);
		if (sc->mfi_tb_ioc_init_dmat != NULL)
			bus_dma_tag_destroy(sc->mfi_tb_ioc_init_dmat);
		for (int i = 0; i < sc->mfi_max_fw_cmds; i++) {
			if (sc->mfi_cmd_pool_tbolt != NULL) {
				if (sc->mfi_cmd_pool_tbolt[i] != NULL) {
					kfree(sc->mfi_cmd_pool_tbolt[i],
					    M_MFIBUF);
					sc->mfi_cmd_pool_tbolt[i] = NULL;
				}
			}
		}
		if (sc->mfi_cmd_pool_tbolt != NULL) {
			kfree(sc->mfi_cmd_pool_tbolt, M_MFIBUF);
			sc->mfi_cmd_pool_tbolt = NULL;
		}
		if (sc->request_desc_pool != NULL) {
			kfree(sc->request_desc_pool, M_MFIBUF);
			sc->request_desc_pool = NULL;
		}
	}
	if (sc->mfi_buffer_dmat != NULL)
		bus_dma_tag_destroy(sc->mfi_buffer_dmat);
	if (sc->mfi_parent_dmat != NULL)
		bus_dma_tag_destroy(sc->mfi_parent_dmat);

#if 0 /* XXX swildner: not sure if we need something like mtx_initialized() */
	if (mtx_initialized(&sc->mfi_io_lock))
#endif
	{
	lockuninit(&sc->mfi_io_lock);
	lockuninit(&sc->mfi_config_lock);
	}

	return;
}

static void
mfi_startup(void *arg)
{
	struct mfi_softc *sc;

	sc = (struct mfi_softc *)arg;

	config_intrhook_disestablish(&sc->mfi_ich);

	sc->mfi_enable_intr(sc);
	lockmgr(&sc->mfi_config_lock, LK_EXCLUSIVE);
	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	mfi_ldprobe(sc);
	if (sc->mfi_flags & MFI_FLAGS_SKINNY)
		mfi_syspdprobe(sc);
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);
	lockmgr(&sc->mfi_config_lock, LK_RELEASE);
}

static void
mfi_intr(void *arg)
{
	struct mfi_softc *sc;
	struct mfi_command *cm;
	uint32_t pi, ci, context;

	sc = (struct mfi_softc *)arg;

	if (sc->mfi_check_clear_intr(sc))
		return;

restart:
	pi = sc->mfi_comms->hw_pi;
	ci = sc->mfi_comms->hw_ci;
	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	while (ci != pi) {
		context = sc->mfi_comms->hw_reply_q[ci];
		if (context < sc->mfi_max_fw_cmds) {
			cm = &sc->mfi_commands[context];
			mfi_remove_busy(cm);
			cm->cm_error = 0;
			mfi_complete(sc, cm);
		}
		if (++ci == (sc->mfi_max_fw_cmds + 1)) {
			ci = 0;
		}
	}

	sc->mfi_comms->hw_ci = ci;

	/* Give defered I/O a chance to run */
	if (sc->mfi_flags & MFI_FLAGS_QFRZN)
		sc->mfi_flags &= ~MFI_FLAGS_QFRZN;
	mfi_startio(sc);
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);

	/*
	 * Dummy read to flush the bus; this ensures that the indexes are up
	 * to date.  Restart processing if more commands have come it.
	 */
	(void)sc->mfi_read_fw_status(sc);
	if (pi != sc->mfi_comms->hw_pi)
		goto restart;

	return;
}

int
mfi_shutdown(struct mfi_softc *sc)
{
	struct mfi_dcmd_frame *dcmd;
	struct mfi_command *cm;
	int error;

	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	error = mfi_dcmd_command(sc, &cm, MFI_DCMD_CTRL_SHUTDOWN, NULL, 0);
	if (error) {
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		return (error);
	}

	if (sc->mfi_aen_cm != NULL)
		mfi_abort(sc, sc->mfi_aen_cm);

	if (sc->map_update_cmd != NULL)
		mfi_abort(sc, sc->map_update_cmd);

	dcmd = &cm->cm_frame->dcmd;
	dcmd->header.flags = MFI_FRAME_DIR_NONE;
	cm->cm_flags = MFI_CMD_POLLED;
	cm->cm_data = NULL;

	if ((error = mfi_mapcmd(sc, cm)) != 0) {
		device_printf(sc->mfi_dev, "Failed to shutdown controller\n");
	}

	mfi_release_command(cm);
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);
	return (error);
}

static void
mfi_syspdprobe(struct mfi_softc *sc)
{
	struct mfi_frame_header *hdr;
	struct mfi_command *cm = NULL;
	struct mfi_pd_list *pdlist = NULL;
	struct mfi_system_pd *syspd, *tmp;
	int error, i, found;

	mfi_lockassert(&sc->mfi_config_lock);
	mfi_lockassert(&sc->mfi_io_lock);
	/* Add SYSTEM PD's */
	error = mfi_dcmd_command(sc, &cm, MFI_DCMD_PD_LIST_QUERY,
	    (void **)&pdlist, sizeof(*pdlist));
	if (error) {
		device_printf(sc->mfi_dev,
		    "Error while forming SYSTEM PD list\n");
		goto out;
	}

	cm->cm_flags = MFI_CMD_DATAIN | MFI_CMD_POLLED;
	cm->cm_frame->dcmd.mbox[0] = MR_PD_QUERY_TYPE_EXPOSED_TO_HOST;
	cm->cm_frame->dcmd.mbox[1] = 0;
	if (mfi_mapcmd(sc, cm) != 0) {
		device_printf(sc->mfi_dev,
		    "Failed to get syspd device listing\n");
		goto out;
	}
	bus_dmamap_sync(sc->mfi_buffer_dmat,cm->cm_dmamap,
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->mfi_buffer_dmat, cm->cm_dmamap);
	hdr = &cm->cm_frame->header;
	if (hdr->cmd_status != MFI_STAT_OK) {
		device_printf(sc->mfi_dev,
		    "MFI_DCMD_PD_LIST_QUERY failed %x\n", hdr->cmd_status);
		goto out;
	}
	/* Get each PD and add it to the system */
	for (i = 0; i < pdlist->count; i++) {
		if (pdlist->addr[i].device_id ==
		    pdlist->addr[i].encl_device_id)
			continue;
		found = 0;
		TAILQ_FOREACH(syspd, &sc->mfi_syspd_tqh, pd_link) {
			if (syspd->pd_id == pdlist->addr[i].device_id)
				found = 1;
		}
		if (found == 0)
			mfi_add_sys_pd(sc, pdlist->addr[i].device_id);
	}
	/* Delete SYSPD's whose state has been changed */
	TAILQ_FOREACH_MUTABLE(syspd, &sc->mfi_syspd_tqh, pd_link, tmp) {
		found = 0;
		for (i = 0; i < pdlist->count; i++) {
			if (syspd->pd_id == pdlist->addr[i].device_id)
				found = 1;
		}
		if (found == 0) {
			kprintf("DELETE\n");
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			get_mplock();
			device_delete_child(sc->mfi_dev, syspd->pd_dev);
			rel_mplock();
			lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		}
	}
out:
	if (pdlist)
		kfree(pdlist, M_MFIBUF);
	if (cm)
		mfi_release_command(cm);
}

static void
mfi_ldprobe(struct mfi_softc *sc)
{
	struct mfi_frame_header *hdr;
	struct mfi_command *cm = NULL;
	struct mfi_ld_list *list = NULL;
	struct mfi_disk *ld;
	int error, i;

	mfi_lockassert(&sc->mfi_config_lock);
	mfi_lockassert(&sc->mfi_io_lock);

	error = mfi_dcmd_command(sc, &cm, MFI_DCMD_LD_GET_LIST,
	    (void **)&list, sizeof(*list));
	if (error)
		goto out;

	cm->cm_flags = MFI_CMD_DATAIN;
	if (mfi_wait_command(sc, cm) != 0) {
		device_printf(sc->mfi_dev, "Failed to get device listing\n");
		goto out;
	}

	hdr = &cm->cm_frame->header;
	if (hdr->cmd_status != MFI_STAT_OK) {
		device_printf(sc->mfi_dev, "MFI_DCMD_LD_GET_LIST failed %x\n",
		    hdr->cmd_status);
		goto out;
	}

	for (i = 0; i < list->ld_count; i++) {
		TAILQ_FOREACH(ld, &sc->mfi_ld_tqh, ld_link) {
			if (ld->ld_id == list->ld_list[i].ld.v.target_id)
				goto skip_add;
		}
		mfi_add_ld(sc, list->ld_list[i].ld.v.target_id);
	skip_add:;
	}
out:
	if (list)
		kfree(list, M_MFIBUF);
	if (cm)
		mfi_release_command(cm);

	return;
}

/*
 * The timestamp is the number of seconds since 00:00 Jan 1, 2000.  If
 * the bits in 24-31 are all set, then it is the number of seconds since
 * boot.
 */
static const char *
format_timestamp(uint32_t timestamp)
{
	static char buffer[32];

	if ((timestamp & 0xff000000) == 0xff000000)
		ksnprintf(buffer, sizeof(buffer), "boot + %us", timestamp &
		    0x00ffffff);
	else
		ksnprintf(buffer, sizeof(buffer), "%us", timestamp);
	return (buffer);
}

static const char *
format_class(int8_t class)
{
	static char buffer[6];

	switch (class) {
	case MFI_EVT_CLASS_DEBUG:
		return ("debug");
	case MFI_EVT_CLASS_PROGRESS:
		return ("progress");
	case MFI_EVT_CLASS_INFO:
		return ("info");
	case MFI_EVT_CLASS_WARNING:
		return ("WARN");
	case MFI_EVT_CLASS_CRITICAL:
		return ("CRIT");
	case MFI_EVT_CLASS_FATAL:
		return ("FATAL");
	case MFI_EVT_CLASS_DEAD:
		return ("DEAD");
	default:
		ksnprintf(buffer, sizeof(buffer), "%d", class);
		return (buffer);
	}
}

static void
mfi_decode_evt(struct mfi_softc *sc, struct mfi_evt_detail *detail)
{
	struct mfi_system_pd *syspd = NULL;

	device_printf(sc->mfi_dev, "%d (%s/0x%04x/%s) - %s\n", detail->seq,
	    format_timestamp(detail->time), detail->evt_class.members.locale,
	    format_class(detail->evt_class.members.evt_class),
	    detail->description);

	/* Don't act on old AEN's or while shutting down */
	if (detail->seq < sc->mfi_boot_seq_num || sc->mfi_detaching)
		return;

	switch (detail->arg_type) {
	case MR_EVT_ARGS_NONE:
		if (detail->code == MR_EVT_CTRL_HOST_BUS_SCAN_REQUESTED) {
		    device_printf(sc->mfi_dev, "HostBus scan raised\n");
			if (mfi_detect_jbod_change) {
				/*
				 * Probe for new SYSPD's and Delete
				 * invalid SYSPD's
				 */
				lockmgr(&sc->mfi_config_lock, LK_EXCLUSIVE);
				lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
				mfi_syspdprobe(sc);
				lockmgr(&sc->mfi_io_lock, LK_RELEASE);
				lockmgr(&sc->mfi_config_lock, LK_RELEASE);
			}
		}
		break;
	case MR_EVT_ARGS_LD_STATE:
		/*
		 * During load time driver reads all the events starting
		 * from the one that has been logged after shutdown. Avoid
		 * these old events.
		 */
		if (detail->args.ld_state.new_state == MFI_LD_STATE_OFFLINE ) {
			/* Remove the LD */
			struct mfi_disk *ld;
			TAILQ_FOREACH(ld, &sc->mfi_ld_tqh, ld_link) {
				if (ld->ld_id ==
				    detail->args.ld_state.ld.target_id)
					break;
			}
			/*
			Fix: for kernel panics when SSCD is removed
			KASSERT(ld != NULL, ("volume dissappeared"));
			*/
			if (ld != NULL) {
				get_mplock();
				device_delete_child(sc->mfi_dev, ld->ld_dev);
				rel_mplock();
			}
		}
		break;
	case MR_EVT_ARGS_PD:
		if (detail->code == MR_EVT_PD_REMOVED) {
			if (mfi_detect_jbod_change) {
				/*
				 * If the removed device is a SYSPD then
				 * delete it
				 */
				TAILQ_FOREACH(syspd, &sc->mfi_syspd_tqh,
				    pd_link) {
					if (syspd->pd_id ==
					    detail->args.pd.device_id) {
						get_mplock();
						device_delete_child(
						    sc->mfi_dev,
						    syspd->pd_dev);
						rel_mplock();
						break;
					}
				}
			}
		}
		if (detail->code == MR_EVT_PD_INSERTED) {
			if (mfi_detect_jbod_change) {
				/* Probe for new SYSPD's */
				lockmgr(&sc->mfi_config_lock, LK_EXCLUSIVE);
				lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
				mfi_syspdprobe(sc);
				lockmgr(&sc->mfi_io_lock, LK_RELEASE);
				lockmgr(&sc->mfi_config_lock, LK_RELEASE);
			}
		}
		break;
	}
}

static void
mfi_queue_evt(struct mfi_softc *sc, struct mfi_evt_detail *detail)
{
	struct mfi_evt_queue_elm *elm;

	mfi_lockassert(&sc->mfi_io_lock);
	elm = kmalloc(sizeof(*elm), M_MFIBUF, M_NOWAIT | M_ZERO);
	if (elm == NULL)
		return;
	memcpy(&elm->detail, detail, sizeof(*detail));
	TAILQ_INSERT_TAIL(&sc->mfi_evt_queue, elm, link);
	taskqueue_enqueue(taskqueue_swi, &sc->mfi_evt_task);
}

static void
mfi_handle_evt(void *context, int pending)
{
	TAILQ_HEAD(,mfi_evt_queue_elm) queue;
	struct mfi_softc *sc;
	struct mfi_evt_queue_elm *elm;

	sc = context;
	TAILQ_INIT(&queue);
	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	TAILQ_CONCAT(&queue, &sc->mfi_evt_queue, link);
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);
	while ((elm = TAILQ_FIRST(&queue)) != NULL) {
		TAILQ_REMOVE(&queue, elm, link);
		mfi_decode_evt(sc, &elm->detail);
		kfree(elm, M_MFIBUF);
	}
}

static int
mfi_aen_register(struct mfi_softc *sc, int seq, int locale)
{
	struct mfi_command *cm;
	struct mfi_dcmd_frame *dcmd;
	union mfi_evt current_aen, prior_aen;
	struct mfi_evt_detail *ed = NULL;
	int error = 0;

	current_aen.word = locale;
	if (sc->mfi_aen_cm != NULL) {
		prior_aen.word =
		    ((uint32_t *)&sc->mfi_aen_cm->cm_frame->dcmd.mbox)[1];
		if (prior_aen.members.evt_class <= current_aen.members.evt_class &&
		    !((prior_aen.members.locale & current_aen.members.locale)
		    ^current_aen.members.locale)) {
			return (0);
		} else {
			prior_aen.members.locale |= current_aen.members.locale;
			if (prior_aen.members.evt_class
			    < current_aen.members.evt_class)
				current_aen.members.evt_class =
				    prior_aen.members.evt_class;
			lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
			mfi_abort(sc, sc->mfi_aen_cm);
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		}
	}

	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	error = mfi_dcmd_command(sc, &cm, MFI_DCMD_CTRL_EVENT_WAIT,
	    (void **)&ed, sizeof(*ed));
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);
	if (error) {
		goto out;
	}

	dcmd = &cm->cm_frame->dcmd;
	((uint32_t *)&dcmd->mbox)[0] = seq;
	((uint32_t *)&dcmd->mbox)[1] = locale;
	cm->cm_flags = MFI_CMD_DATAIN;
	cm->cm_complete = mfi_aen_complete;

	sc->last_seq_num = seq;
	sc->mfi_aen_cm = cm;

	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	mfi_enqueue_ready(cm);
	mfi_startio(sc);
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);

out:
	return (error);
}

static void
mfi_aen_complete(struct mfi_command *cm)
{
	struct mfi_frame_header *hdr;
	struct mfi_softc *sc;
	struct mfi_evt_detail *detail;
	struct mfi_aen *mfi_aen_entry, *tmp;
	struct proc *p;
	int seq = 0, aborted = 0;

	sc = cm->cm_sc;
	mfi_lockassert(&sc->mfi_io_lock);

	hdr = &cm->cm_frame->header;

	if (sc->mfi_aen_cm == NULL)
		return;

	if (sc->mfi_aen_cm->cm_aen_abort ||
	    hdr->cmd_status == MFI_STAT_INVALID_STATUS) {
		sc->mfi_aen_cm->cm_aen_abort = 0;
		aborted = 1;
	} else {
		sc->mfi_aen_triggered = 1;
		if (sc->mfi_poll_waiting) {
			sc->mfi_poll_waiting = 0;
			KNOTE(&sc->mfi_kq.ki_note, 0);
		}
		detail = cm->cm_data;
		mfi_queue_evt(sc, detail);
		seq = detail->seq + 1;
		TAILQ_FOREACH_MUTABLE(mfi_aen_entry, &sc->mfi_aen_pids,
		    aen_link, tmp) {
			TAILQ_REMOVE(&sc->mfi_aen_pids, mfi_aen_entry,
				     aen_link);
			p = mfi_aen_entry->p;
			PHOLD(p);
			ksignal(p, SIGIO);
			PRELE(p);
			kfree(mfi_aen_entry, M_MFIBUF);
		}
	}

	kfree(cm->cm_data, M_MFIBUF);
	sc->mfi_aen_cm = NULL;
	wakeup(&sc->mfi_aen_cm);
	mfi_release_command(cm);

	/* set it up again so the driver can catch more events */
	if (!aborted) {
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		mfi_aen_setup(sc, seq);
		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	}
}

#define MAX_EVENTS 15

static int
mfi_parse_entries(struct mfi_softc *sc, int start_seq, int stop_seq)
{
	struct mfi_command *cm;
	struct mfi_dcmd_frame *dcmd;
	struct mfi_evt_list *el;
	union mfi_evt class_locale;
	int error, i, seq, size;

	class_locale.members.reserved = 0;
	class_locale.members.locale = mfi_event_locale;
	class_locale.members.evt_class  = mfi_event_class;

	size = sizeof(struct mfi_evt_list) + sizeof(struct mfi_evt_detail)
		* (MAX_EVENTS - 1);
	el = kmalloc(size, M_MFIBUF, M_NOWAIT | M_ZERO);
	if (el == NULL)
		return (ENOMEM);

	for (seq = start_seq;;) {
		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		if ((cm = mfi_dequeue_free(sc)) == NULL) {
			kfree(el, M_MFIBUF);
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			return (EBUSY);
		}

		lockmgr(&sc->mfi_io_lock, LK_RELEASE);

		dcmd = &cm->cm_frame->dcmd;
		bzero(dcmd->mbox, MFI_MBOX_SIZE);
		dcmd->header.cmd = MFI_CMD_DCMD;
		dcmd->header.timeout = 0;
		dcmd->header.data_len = size;
		dcmd->opcode = MFI_DCMD_CTRL_EVENT_GET;
		((uint32_t *)&dcmd->mbox)[0] = seq;
		((uint32_t *)&dcmd->mbox)[1] = class_locale.word;
		cm->cm_sg = &dcmd->sgl;
		cm->cm_total_frame_size = MFI_DCMD_FRAME_SIZE;
		cm->cm_flags = MFI_CMD_DATAIN | MFI_CMD_POLLED;
		cm->cm_data = el;
		cm->cm_len = size;

		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		if ((error = mfi_mapcmd(sc, cm)) != 0) {
			device_printf(sc->mfi_dev,
			    "Failed to get controller entries\n");
			mfi_release_command(cm);
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			break;
		}

		lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		bus_dmamap_sync(sc->mfi_buffer_dmat, cm->cm_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->mfi_buffer_dmat, cm->cm_dmamap);

		if (dcmd->header.cmd_status == MFI_STAT_NOT_FOUND) {
			lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
			mfi_release_command(cm);
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			break;
		}
		if (dcmd->header.cmd_status != MFI_STAT_OK) {
			device_printf(sc->mfi_dev,
			    "Error %d fetching controller entries\n",
			    dcmd->header.cmd_status);
			lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
			mfi_release_command(cm);
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			break;
		}
		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		mfi_release_command(cm);
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);

		for (i = 0; i < el->count; i++) {
			/*
			 * If this event is newer than 'stop_seq' then
			 * break out of the loop.  Note that the log
			 * is a circular buffer so we have to handle
			 * the case that our stop point is earlier in
			 * the buffer than our start point.
			 */
			if (el->event[i].seq >= stop_seq) {
				if (start_seq <= stop_seq)
					break;
				else if (el->event[i].seq < start_seq)
					break;
			}
			lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
			mfi_queue_evt(sc, &el->event[i]);
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		}
		seq = el->event[el->count - 1].seq + 1;
	}

	kfree(el, M_MFIBUF);
	return (0);
}

static int
mfi_add_ld(struct mfi_softc *sc, int id)
{
	struct mfi_command *cm;
	struct mfi_dcmd_frame *dcmd = NULL;
	struct mfi_ld_info *ld_info = NULL;
	int error;

	mfi_lockassert(&sc->mfi_io_lock);

	error = mfi_dcmd_command(sc, &cm, MFI_DCMD_LD_GET_INFO,
	    (void **)&ld_info, sizeof(*ld_info));
	if (error) {
		device_printf(sc->mfi_dev,
		    "Failed to allocate for MFI_DCMD_LD_GET_INFO %d\n", error);
		if (ld_info)
			kfree(ld_info, M_MFIBUF);
		return (error);
	}
	cm->cm_flags = MFI_CMD_DATAIN;
	dcmd = &cm->cm_frame->dcmd;
	dcmd->mbox[0] = id;
	if (mfi_wait_command(sc, cm) != 0) {
		device_printf(sc->mfi_dev,
		    "Failed to get logical drive: %d\n", id);
		kfree(ld_info, M_MFIBUF);
		return (0);
	}
	if (ld_info->ld_config.params.isSSCD != 1) {
		mfi_add_ld_complete(cm);
	} else {
		mfi_release_command(cm);
		if (ld_info)		/* SSCD drives ld_info free here */
			kfree(ld_info, M_MFIBUF);
	}
	return (0);
}

static void
mfi_add_ld_complete(struct mfi_command *cm)
{
	struct mfi_frame_header *hdr;
	struct mfi_ld_info *ld_info;
	struct mfi_softc *sc;
	device_t child;

	sc = cm->cm_sc;
	hdr = &cm->cm_frame->header;
	ld_info = cm->cm_private;

	if (hdr->cmd_status != MFI_STAT_OK) {
		kfree(ld_info, M_MFIBUF);
		mfi_release_command(cm);
		return;
	}
	mfi_release_command(cm);

	lockmgr(&sc->mfi_io_lock, LK_RELEASE);
	get_mplock();
	if ((child = device_add_child(sc->mfi_dev, "mfid", -1)) == NULL) {
		device_printf(sc->mfi_dev, "Failed to add logical disk\n");
		kfree(ld_info, M_MFIBUF);
		rel_mplock();
		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		return;
	}

	device_set_ivars(child, ld_info);
	device_set_desc(child, "MFI Logical Disk");
	bus_generic_attach(sc->mfi_dev);
	rel_mplock();
	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
}

static int
mfi_add_sys_pd(struct mfi_softc *sc, int id)
{
	struct mfi_command *cm;
	struct mfi_dcmd_frame *dcmd = NULL;
	struct mfi_pd_info *pd_info = NULL;
	int error;

	mfi_lockassert(&sc->mfi_io_lock);

	error = mfi_dcmd_command(sc, &cm, MFI_DCMD_PD_GET_INFO,
	    (void **)&pd_info, sizeof(*pd_info));
	if (error) {
		device_printf(sc->mfi_dev,
		    "Failed to allocated for MFI_DCMD_PD_GET_INFO %d\n",
		    error);
		if (pd_info)
			kfree(pd_info, M_MFIBUF);
		return (error);
	}
	cm->cm_flags = MFI_CMD_DATAIN | MFI_CMD_POLLED;
	dcmd = &cm->cm_frame->dcmd;
	dcmd->mbox[0] = id;
	dcmd->header.scsi_status = 0;
	dcmd->header.pad0 = 0;
	if (mfi_mapcmd(sc, cm) != 0) {
		device_printf(sc->mfi_dev,
		    "Failed to get physical drive info %d\n", id);
		kfree(pd_info, M_MFIBUF);
		return (0);
	}
	bus_dmamap_sync(sc->mfi_buffer_dmat, cm->cm_dmamap,
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->mfi_buffer_dmat, cm->cm_dmamap);
	mfi_add_sys_pd_complete(cm);
	return (0);
}

static void
mfi_add_sys_pd_complete(struct mfi_command *cm)
{
	struct mfi_frame_header *hdr;
	struct mfi_pd_info *pd_info;
	struct mfi_softc *sc;
	device_t child;

	sc = cm->cm_sc;
	hdr = &cm->cm_frame->header;
	pd_info = cm->cm_private;

	if (hdr->cmd_status != MFI_STAT_OK) {
		kfree(pd_info, M_MFIBUF);
		mfi_release_command(cm);
		return;
	}
	if (pd_info->fw_state != MFI_PD_STATE_SYSTEM) {
		device_printf(sc->mfi_dev, "PD=%x is not SYSTEM PD\n",
		    pd_info->ref.v.device_id);
		kfree(pd_info, M_MFIBUF);
		mfi_release_command(cm);
		return;
	}
	mfi_release_command(cm);

	lockmgr(&sc->mfi_io_lock, LK_RELEASE);
	get_mplock();
	if ((child = device_add_child(sc->mfi_dev, "mfisyspd", -1)) == NULL) {
		device_printf(sc->mfi_dev, "Failed to add system pd\n");
		kfree(pd_info, M_MFIBUF);
		rel_mplock();
		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		return;
	}

	device_set_ivars(child, pd_info);
	device_set_desc(child, "MFI System PD");
	bus_generic_attach(sc->mfi_dev);
	rel_mplock();
	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
}

static struct mfi_command *
mfi_bio_command(struct mfi_softc *sc)
{
	struct bio *bio;
	struct mfi_command *cm = NULL;
	struct mfi_disk *mfid;

	/* reserving two commands to avoid starvation for IOCTL */
	if (sc->mfi_qstat[MFIQ_FREE].q_length < 2)
		return (NULL);
	if ((bio = mfi_dequeue_bio(sc)) == NULL)
		return (NULL);
	mfid = bio->bio_driver_info;
	if (mfid->ld_flags & MFI_DISK_FLAGS_SYSPD)
		cm = mfi_build_syspdio(sc, bio);
	else
		cm = mfi_build_ldio(sc, bio);
	if (!cm)
		mfi_enqueue_bio(sc, bio);
	return cm;
}

static struct mfi_command *
mfi_build_syspdio(struct mfi_softc *sc, struct bio *bio)
{
	struct mfi_command *cm;
	struct buf *bp;
	struct mfi_system_pd *disk;
	struct mfi_pass_frame *pass;
	int flags = 0, blkcount = 0;
	uint32_t context = 0;

	if ((cm = mfi_dequeue_free(sc)) == NULL)
		return (NULL);

	/* Zero out the MFI frame */
	context = cm->cm_frame->header.context;
	bzero(cm->cm_frame, sizeof(union mfi_frame));
	cm->cm_frame->header.context = context;
	bp = bio->bio_buf;
	pass = &cm->cm_frame->pass;
	bzero(pass->cdb, 16);
	pass->header.cmd = MFI_CMD_PD_SCSI_IO;
	switch (bp->b_cmd & 0x03) {
	case BUF_CMD_READ:
		pass->cdb[0] = READ_10;
		flags = MFI_CMD_DATAIN;
		break;
	case BUF_CMD_WRITE:
		pass->cdb[0] = WRITE_10;
		flags = MFI_CMD_DATAOUT;
		break;
	default:
		panic("Invalid bio command");
	}

	/* Cheat with the sector length to avoid a non-constant division */
	blkcount = (bp->b_bcount + MFI_SECTOR_LEN - 1) / MFI_SECTOR_LEN;
	disk = bio->bio_driver_info;
	/* Fill the LBA and Transfer length in CDB */
	pass->cdb[2] = ((bio->bio_offset / MFI_SECTOR_LEN) & 0xff000000) >> 24;
	pass->cdb[3] = ((bio->bio_offset / MFI_SECTOR_LEN) & 0x00ff0000) >> 16;
	pass->cdb[4] = ((bio->bio_offset / MFI_SECTOR_LEN) & 0x0000ff00) >> 8;
	pass->cdb[5] = (bio->bio_offset / MFI_SECTOR_LEN) & 0x000000ff;
	pass->cdb[7] = (blkcount & 0xff00) >> 8;
	pass->cdb[8] = (blkcount & 0x00ff);
	pass->header.target_id = disk->pd_id;
	pass->header.timeout = 0;
	pass->header.flags = 0;
	pass->header.scsi_status = 0;
	pass->header.sense_len = MFI_SENSE_LEN;
	pass->header.data_len = bp->b_bcount;
	pass->header.cdb_len = 10;
	pass->sense_addr_lo = (uint32_t)cm->cm_sense_busaddr;
	pass->sense_addr_hi = (uint32_t)((uint64_t)cm->cm_sense_busaddr >> 32);
	cm->cm_complete = mfi_bio_complete;
	cm->cm_private = bio;
	cm->cm_data = bp->b_data;
	cm->cm_len = bp->b_bcount;
	cm->cm_sg = &pass->sgl;
	cm->cm_total_frame_size = MFI_PASS_FRAME_SIZE;
	cm->cm_flags = flags;
	return (cm);
}

static struct mfi_command *
mfi_build_ldio(struct mfi_softc *sc, struct bio *bio)
{
	struct mfi_io_frame *io;
	struct buf *bp;
	struct mfi_disk *disk;
	struct mfi_command *cm;
	int flags, blkcount;
	uint32_t context = 0;

	if ((cm = mfi_dequeue_free(sc)) == NULL)
	    return (NULL);

	/* Zero out the MFI frame */
	context = cm->cm_frame->header.context;
	bzero(cm->cm_frame, sizeof(union mfi_frame));
	cm->cm_frame->header.context = context;
	bp = bio->bio_buf;
	io = &cm->cm_frame->io;
	switch (bp->b_cmd & 0x03) {
	case BUF_CMD_READ:
		io->header.cmd = MFI_CMD_LD_READ;
		flags = MFI_CMD_DATAIN;
		break;
	case BUF_CMD_WRITE:
		io->header.cmd = MFI_CMD_LD_WRITE;
		flags = MFI_CMD_DATAOUT;
		break;
	default:
		panic("Invalid bio command");
	}

	/* Cheat with the sector length to avoid a non-constant division */
	blkcount = (bp->b_bcount + MFI_SECTOR_LEN - 1) / MFI_SECTOR_LEN;
	disk = bio->bio_driver_info;
	io->header.target_id = disk->ld_id;
	io->header.timeout = 0;
	io->header.flags = 0;
	io->header.scsi_status = 0;
	io->header.sense_len = MFI_SENSE_LEN;
	io->header.data_len = blkcount;
	io->sense_addr_lo = (uint32_t)cm->cm_sense_busaddr;
	io->sense_addr_hi = (uint32_t)((uint64_t)cm->cm_sense_busaddr >> 32);
	io->lba_hi = ((bio->bio_offset / MFI_SECTOR_LEN) & 0xffffffff00000000) >> 32;
	io->lba_lo = (bio->bio_offset / MFI_SECTOR_LEN) & 0xffffffff;
	cm->cm_complete = mfi_bio_complete;
	cm->cm_private = bio;
	cm->cm_data = bp->b_data;
	cm->cm_len = bp->b_bcount;
	cm->cm_sg = &io->sgl;
	cm->cm_total_frame_size = MFI_IO_FRAME_SIZE;
	cm->cm_flags = flags;
	return (cm);
}

static void
mfi_bio_complete(struct mfi_command *cm)
{
	struct bio *bio;
	struct buf *bp;
	struct mfi_frame_header *hdr;
	struct mfi_softc *sc;

	bio = cm->cm_private;
	bp = bio->bio_buf;
	hdr = &cm->cm_frame->header;
	sc = cm->cm_sc;

	if ((hdr->cmd_status != MFI_STAT_OK) || (hdr->scsi_status != 0)) {
		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
		device_printf(sc->mfi_dev, "I/O error, status= %d "
		    "scsi_status= %d\n", hdr->cmd_status, hdr->scsi_status);
		mfi_print_sense(cm->cm_sc, cm->cm_sense);
	} else if (cm->cm_error != 0) {
		bp->b_flags |= B_ERROR;
	}

	mfi_release_command(cm);
	mfi_disk_complete(bio);
}

void
mfi_startio(struct mfi_softc *sc)
{
	struct mfi_command *cm;
	struct ccb_hdr *ccbh;

	for (;;) {
		/* Don't bother if we're short on resources */
		if (sc->mfi_flags & MFI_FLAGS_QFRZN)
			break;

		/* Try a command that has already been prepared */
		cm = mfi_dequeue_ready(sc);

		if (cm == NULL) {
			if ((ccbh = TAILQ_FIRST(&sc->mfi_cam_ccbq)) != NULL)
				cm = sc->mfi_cam_start(ccbh);
		}

		/* Nope, so look for work on the bioq */
		if (cm == NULL)
			cm = mfi_bio_command(sc);

		/* No work available, so exit */
		if (cm == NULL)
			break;

		/* Send the command to the controller */
		if (mfi_mapcmd(sc, cm) != 0) {
			mfi_requeue_ready(cm);
			break;
		}
	}
}

int
mfi_mapcmd(struct mfi_softc *sc, struct mfi_command *cm)
{
	int error, polled;

	mfi_lockassert(&sc->mfi_io_lock);

	if ((cm->cm_data != NULL) && (cm->cm_frame->header.cmd != MFI_CMD_STP)) {
		polled = (cm->cm_flags & MFI_CMD_POLLED) ? BUS_DMA_NOWAIT : 0;
		error = bus_dmamap_load(sc->mfi_buffer_dmat, cm->cm_dmamap,
		    cm->cm_data, cm->cm_len, mfi_data_cb, cm, polled);
		if (error == EINPROGRESS) {
			sc->mfi_flags |= MFI_FLAGS_QFRZN;
			return (0);
		}
	} else {
		if (sc->MFA_enabled)
			error = mfi_tbolt_send_frame(sc, cm);
		else
			error = mfi_send_frame(sc, cm);
	}

	return (error);
}

static void
mfi_data_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct mfi_frame_header *hdr;
	struct mfi_command *cm;
	union mfi_sgl *sgl;
	struct mfi_softc *sc;
	int i, j, first, dir;

	cm = (struct mfi_command *)arg;
	sc = cm->cm_sc;
	hdr = &cm->cm_frame->header;
	sgl = cm->cm_sg;

	if (error) {
		kprintf("error %d in callback\n", error);
		cm->cm_error = error;
		mfi_complete(sc, cm);
		return;
	}

	/* Use IEEE sgl only for IO's on a SKINNY controller
	 * For other commands on a SKINNY controller use either
	 * sg32 or sg64 based on the sizeof(bus_addr_t).
	 * Also calculate the total frame size based on the type
	 * of SGL used.
	 */
	if (((cm->cm_frame->header.cmd == MFI_CMD_PD_SCSI_IO) ||
	     (cm->cm_frame->header.cmd == MFI_CMD_LD_READ) ||
	     (cm->cm_frame->header.cmd == MFI_CMD_LD_WRITE)) &&
	    (sc->mfi_flags & MFI_FLAGS_SKINNY)) {
		for (i = 0; i < nsegs; i++) {
			sgl->sg_skinny[i].addr = segs[i].ds_addr;
			sgl->sg_skinny[i].len = segs[i].ds_len;
			sgl->sg_skinny[i].flag = 0;
		}
		hdr->flags |= MFI_FRAME_IEEE_SGL | MFI_FRAME_SGL64;
		hdr->sg_count = nsegs;
	} else {
		j = 0;
		if (cm->cm_frame->header.cmd == MFI_CMD_STP) {
			first = cm->cm_stp_len;
			if ((sc->mfi_flags & MFI_FLAGS_SG64) == 0) {
				sgl->sg32[j].addr = segs[0].ds_addr;
				sgl->sg32[j++].len = first;
			} else {
				sgl->sg64[j].addr = segs[0].ds_addr;
				sgl->sg64[j++].len = first;
			}
		} else
			first = 0;
		if ((sc->mfi_flags & MFI_FLAGS_SG64) == 0) {
			for (i = 0; i < nsegs; i++) {
				sgl->sg32[j].addr = segs[i].ds_addr + first;
				sgl->sg32[j++].len = segs[i].ds_len - first;
				first = 0;
			}
		} else {
			for (i = 0; i < nsegs; i++) {
				sgl->sg64[j].addr = segs[i].ds_addr + first;
				sgl->sg64[j++].len = segs[i].ds_len - first;
				first = 0;
			}
			hdr->flags |= MFI_FRAME_SGL64;
		}
		hdr->sg_count = j;
	}

	dir = 0;
	if (cm->cm_flags & MFI_CMD_DATAIN) {
		dir |= BUS_DMASYNC_PREREAD;
		hdr->flags |= MFI_FRAME_DIR_READ;
	}
	if (cm->cm_flags & MFI_CMD_DATAOUT) {
		dir |= BUS_DMASYNC_PREWRITE;
		hdr->flags |= MFI_FRAME_DIR_WRITE;
	}
	bus_dmamap_sync(sc->mfi_buffer_dmat, cm->cm_dmamap, dir);
	cm->cm_flags |= MFI_CMD_MAPPED;

	/*
	 * Instead of calculating the total number of frames in the
	 * compound frame, it's already assumed that there will be at
	 * least 1 frame, so don't compensate for the modulo of the
	 * following division.
	 */
	cm->cm_total_frame_size += (sc->mfi_sge_size * nsegs);
	cm->cm_extra_frames = (cm->cm_total_frame_size - 1) / MFI_FRAME_SIZE;

	if (sc->MFA_enabled)
		mfi_tbolt_send_frame(sc, cm);
	else
		mfi_send_frame(sc, cm);
}

static int
mfi_send_frame(struct mfi_softc *sc, struct mfi_command *cm)
{
	struct mfi_frame_header *hdr;
	int tm = MFI_POLL_TIMEOUT_SECS * 1000;

	hdr = &cm->cm_frame->header;

	if ((cm->cm_flags & MFI_CMD_POLLED) == 0) {
		cm->cm_timestamp = time_uptime;
		mfi_enqueue_busy(cm);
	} else {
		hdr->cmd_status = MFI_STAT_INVALID_STATUS;
		hdr->flags |= MFI_FRAME_DONT_POST_IN_REPLY_QUEUE;
	}

	/*
	 * The bus address of the command is aligned on a 64 byte boundary,
	 * leaving the least 6 bits as zero.  For whatever reason, the
	 * hardware wants the address shifted right by three, leaving just
	 * 3 zero bits.  These three bits are then used as a prefetching
	 * hint for the hardware to predict how many frames need to be
	 * fetched across the bus.  If a command has more than 8 frames
	 * then the 3 bits are set to 0x7 and the firmware uses other
	 * information in the command to determine the total amount to fetch.
	 * However, FreeBSD doesn't support I/O larger than 128K, so 8 frames
	 * is enough for both 32bit and 64bit systems.
	 */
	if (cm->cm_extra_frames > 7)
		cm->cm_extra_frames = 7;

	sc->mfi_issue_cmd(sc, cm->cm_frame_busaddr, cm->cm_extra_frames);

	if ((cm->cm_flags & MFI_CMD_POLLED) == 0)
		return (0);

	/* This is a polled command, so busy-wait for it to complete. */
	while (hdr->cmd_status == MFI_STAT_INVALID_STATUS) {
		DELAY(1000);
		tm -= 1;
		if (tm <= 0)
			break;
	}

	if (hdr->cmd_status == MFI_STAT_INVALID_STATUS) {
		device_printf(sc->mfi_dev, "Frame %p timed out "
		    "command 0x%X\n", hdr, cm->cm_frame->dcmd.opcode);
		return (ETIMEDOUT);
	}

	return (0);
}

void
mfi_complete(struct mfi_softc *sc, struct mfi_command *cm)
{
	int dir;

	if ((cm->cm_flags & MFI_CMD_MAPPED) != 0) {
		dir = 0;
		if ((cm->cm_flags & MFI_CMD_DATAIN) ||
		    (cm->cm_frame->header.cmd == MFI_CMD_STP))
			dir |= BUS_DMASYNC_POSTREAD;
		if (cm->cm_flags & MFI_CMD_DATAOUT)
			dir |= BUS_DMASYNC_POSTWRITE;

		bus_dmamap_sync(sc->mfi_buffer_dmat, cm->cm_dmamap, dir);
		bus_dmamap_unload(sc->mfi_buffer_dmat, cm->cm_dmamap);
		cm->cm_flags &= ~MFI_CMD_MAPPED;
	}

	cm->cm_flags |= MFI_CMD_COMPLETED;

	if (cm->cm_complete != NULL)
		cm->cm_complete(cm);
	else
		wakeup(cm);
}

static int
mfi_abort(struct mfi_softc *sc, struct mfi_command *cm_abort)
{
	struct mfi_command *cm;
	struct mfi_abort_frame *abort;
	int i = 0;
	uint32_t context = 0;

	mfi_lockassert(&sc->mfi_io_lock);

	if ((cm = mfi_dequeue_free(sc)) == NULL) {
		return (EBUSY);
	}

	/* Zero out the MFI frame */
	context = cm->cm_frame->header.context;
	bzero(cm->cm_frame, sizeof(union mfi_frame));
	cm->cm_frame->header.context = context;

	abort = &cm->cm_frame->abort;
	abort->header.cmd = MFI_CMD_ABORT;
	abort->header.flags = 0;
	abort->header.scsi_status = 0;
	abort->abort_context = cm_abort->cm_frame->header.context;
	abort->abort_mfi_addr_lo = (uint32_t)cm_abort->cm_frame_busaddr;
	abort->abort_mfi_addr_hi =
	    (uint32_t)((uint64_t)cm_abort->cm_frame_busaddr >> 32);
	cm->cm_data = NULL;
	cm->cm_flags = MFI_CMD_POLLED;

	if (sc->mfi_aen_cm)
		sc->mfi_aen_cm->cm_aen_abort = 1;
	mfi_mapcmd(sc, cm);
	mfi_release_command(cm);

	while (i < 5 && sc->mfi_aen_cm != NULL) {
		lksleep(&sc->mfi_aen_cm, &sc->mfi_io_lock, 0, "mfiabort",
		    5 * hz);
		i++;
	}

	return (0);
}

int
mfi_dump_blocks(struct mfi_softc *sc, int id, uint64_t lba, void *virt,
    int len)
{
	struct mfi_command *cm;
	struct mfi_io_frame *io;
	int error;
	uint32_t context = 0;

	if ((cm = mfi_dequeue_free(sc)) == NULL)
		return (EBUSY);

	/* Zero out the MFI frame */
	context = cm->cm_frame->header.context;
	bzero(cm->cm_frame, sizeof(union mfi_frame));
	cm->cm_frame->header.context = context;

	io = &cm->cm_frame->io;
	io->header.cmd = MFI_CMD_LD_WRITE;
	io->header.target_id = id;
	io->header.timeout = 0;
	io->header.flags = 0;
	io->header.scsi_status = 0;
	io->header.sense_len = MFI_SENSE_LEN;
	io->header.data_len = (len + MFI_SECTOR_LEN - 1) / MFI_SECTOR_LEN;
	io->sense_addr_lo = (uint32_t)cm->cm_sense_busaddr;
	io->sense_addr_hi = (uint32_t)((uint64_t)cm->cm_sense_busaddr >> 32);
	io->lba_hi = (lba & 0xffffffff00000000) >> 32;
	io->lba_lo = lba & 0xffffffff;
	cm->cm_data = virt;
	cm->cm_len = len;
	cm->cm_sg = &io->sgl;
	cm->cm_total_frame_size = MFI_IO_FRAME_SIZE;
	cm->cm_flags = MFI_CMD_POLLED | MFI_CMD_DATAOUT;

	error = mfi_mapcmd(sc, cm);
	bus_dmamap_sync(sc->mfi_buffer_dmat, cm->cm_dmamap,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->mfi_buffer_dmat, cm->cm_dmamap);
	mfi_release_command(cm);

	return (error);
}

int
mfi_dump_syspd_blocks(struct mfi_softc *sc, int id, uint64_t lba, void *virt,
    int len)
{
	struct mfi_command *cm;
	struct mfi_pass_frame *pass;
	int error;
	int blkcount = 0;

	if ((cm = mfi_dequeue_free(sc)) == NULL)
		return (EBUSY);

	pass = &cm->cm_frame->pass;
	bzero(pass->cdb, 16);
	pass->header.cmd = MFI_CMD_PD_SCSI_IO;
	pass->cdb[0] = WRITE_10;
	pass->cdb[2] = (lba & 0xff000000) >> 24;
	pass->cdb[3] = (lba & 0x00ff0000) >> 16;
	pass->cdb[4] = (lba & 0x0000ff00) >> 8;
	pass->cdb[5] = (lba & 0x000000ff);
	blkcount = (len + MFI_SECTOR_LEN - 1) / MFI_SECTOR_LEN;
	pass->cdb[7] = (blkcount & 0xff00) >> 8;
	pass->cdb[8] = (blkcount & 0x00ff);
	pass->header.target_id = id;
	pass->header.timeout = 0;
	pass->header.flags = 0;
	pass->header.scsi_status = 0;
	pass->header.sense_len = MFI_SENSE_LEN;
	pass->header.data_len = len;
	pass->header.cdb_len = 10;
	pass->sense_addr_lo = (uint32_t)cm->cm_sense_busaddr;
	pass->sense_addr_hi = (uint32_t)((uint64_t)cm->cm_sense_busaddr >> 32);
	cm->cm_data = virt;
	cm->cm_len = len;
	cm->cm_sg = &pass->sgl;
	cm->cm_total_frame_size = MFI_PASS_FRAME_SIZE;
	cm->cm_flags = MFI_CMD_POLLED | MFI_CMD_DATAOUT;

	error = mfi_mapcmd(sc, cm);
	bus_dmamap_sync(sc->mfi_buffer_dmat, cm->cm_dmamap,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->mfi_buffer_dmat, cm->cm_dmamap);
	mfi_release_command(cm);

	return (error);
}

static int
mfi_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct mfi_softc *sc;
	int error;

	sc = dev->si_drv1;

	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	if (sc->mfi_detaching)
		error = ENXIO;
	else {
		sc->mfi_flags |= MFI_FLAGS_OPEN;
		error = 0;
	}
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);

	return (error);
}

static int
mfi_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct mfi_softc *sc;
	struct mfi_aen *mfi_aen_entry, *tmp;

	sc = dev->si_drv1;

	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	sc->mfi_flags &= ~MFI_FLAGS_OPEN;

	TAILQ_FOREACH_MUTABLE(mfi_aen_entry, &sc->mfi_aen_pids, aen_link, tmp) {
		if (mfi_aen_entry->p == curproc) {
			TAILQ_REMOVE(&sc->mfi_aen_pids, mfi_aen_entry,
			    aen_link);
			kfree(mfi_aen_entry, M_MFIBUF);
		}
	}
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);
	return (0);
}

static int
mfi_config_lock(struct mfi_softc *sc, uint32_t opcode)
{

	switch (opcode) {
	case MFI_DCMD_LD_DELETE:
	case MFI_DCMD_CFG_ADD:
	case MFI_DCMD_CFG_CLEAR:
	case MFI_DCMD_CFG_FOREIGN_IMPORT:
		lockmgr(&sc->mfi_config_lock, LK_EXCLUSIVE);
		return (1);
	default:
		return (0);
	}
}

static void
mfi_config_unlock(struct mfi_softc *sc, int locked)
{

	if (locked)
		lockmgr(&sc->mfi_config_lock, LK_RELEASE);
}

/*
 * Perform pre-issue checks on commands from userland and possibly veto
 * them.
 */
static int
mfi_check_command_pre(struct mfi_softc *sc, struct mfi_command *cm)
{
	struct mfi_disk *ld, *ld2;
	int error;
	struct mfi_system_pd *syspd = NULL;
	uint16_t syspd_id;
	uint16_t *mbox;

	mfi_lockassert(&sc->mfi_io_lock);
	error = 0;
	switch (cm->cm_frame->dcmd.opcode) {
	case MFI_DCMD_LD_DELETE:
		TAILQ_FOREACH(ld, &sc->mfi_ld_tqh, ld_link) {
			if (ld->ld_id == cm->cm_frame->dcmd.mbox[0])
				break;
		}
		if (ld == NULL)
			error = ENOENT;
		else
			error = mfi_disk_disable(ld);
		break;
	case MFI_DCMD_CFG_CLEAR:
		TAILQ_FOREACH(ld, &sc->mfi_ld_tqh, ld_link) {
			error = mfi_disk_disable(ld);
			if (error)
				break;
		}
		if (error) {
			TAILQ_FOREACH(ld2, &sc->mfi_ld_tqh, ld_link) {
				if (ld2 == ld)
					break;
				mfi_disk_enable(ld2);
			}
		}
		break;
	case MFI_DCMD_PD_STATE_SET:
		mbox = (uint16_t *)cm->cm_frame->dcmd.mbox;
		syspd_id = mbox[0];
		if (mbox[2] == MFI_PD_STATE_UNCONFIGURED_GOOD) {
			TAILQ_FOREACH(syspd, &sc->mfi_syspd_tqh, pd_link) {
				if (syspd->pd_id == syspd_id)
					break;
			}
		} else {
			break;
		}
		if (syspd)
			error = mfi_syspd_disable(syspd);
		break;
	default:
		break;
	}
	return (error);
}

/* Perform post-issue checks on commands from userland. */
static void
mfi_check_command_post(struct mfi_softc *sc, struct mfi_command *cm)
{
	struct mfi_disk *ld, *ldn;
	struct mfi_system_pd *syspd = NULL;
	uint16_t syspd_id;
	uint16_t *mbox;

	switch (cm->cm_frame->dcmd.opcode) {
	case MFI_DCMD_LD_DELETE:
		TAILQ_FOREACH(ld, &sc->mfi_ld_tqh, ld_link) {
			if (ld->ld_id == cm->cm_frame->dcmd.mbox[0])
				break;
		}
		KASSERT(ld != NULL, ("volume dissappeared"));
		if (cm->cm_frame->header.cmd_status == MFI_STAT_OK) {
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			get_mplock();
			device_delete_child(sc->mfi_dev, ld->ld_dev);
			rel_mplock();
			lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		} else
			mfi_disk_enable(ld);
		break;
	case MFI_DCMD_CFG_CLEAR:
		if (cm->cm_frame->header.cmd_status == MFI_STAT_OK) {
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			get_mplock();
			TAILQ_FOREACH_MUTABLE(ld, &sc->mfi_ld_tqh, ld_link, ldn) {
				device_delete_child(sc->mfi_dev, ld->ld_dev);
			}
			rel_mplock();
			lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		} else {
			TAILQ_FOREACH(ld, &sc->mfi_ld_tqh, ld_link)
				mfi_disk_enable(ld);
		}
		break;
	case MFI_DCMD_CFG_ADD:
	case MFI_DCMD_CFG_FOREIGN_IMPORT:
		if (cm->cm_frame->header.cmd_status == MFI_STAT_OK)
			mfi_ldprobe(sc);
		break;
	case MFI_DCMD_PD_STATE_SET:
		mbox = (uint16_t *)cm->cm_frame->dcmd.mbox;
		syspd_id = mbox[0];
		if (mbox[2] == MFI_PD_STATE_UNCONFIGURED_GOOD) {
			TAILQ_FOREACH(syspd, &sc->mfi_syspd_tqh, pd_link) {
				if (syspd->pd_id == syspd_id)
					break;
			}
		} else {
			break;
		}
		/* If the transition fails then enable the syspd again */
		if (syspd && cm->cm_frame->header.cmd_status != MFI_STAT_OK)
			mfi_syspd_enable(syspd);
		break;
	}
}

static int
mfi_check_for_sscd(struct mfi_softc *sc, struct mfi_command *cm)
{
	struct mfi_config_data *conf_data = cm->cm_data;
	struct mfi_command *ld_cm = NULL;
	struct mfi_ld_info *ld_info = NULL;
	int error = 0;

	if ((cm->cm_frame->dcmd.opcode == MFI_DCMD_CFG_ADD) &&
	    (conf_data->ld[0].params.isSSCD == 1)) {
		error = 1;
	} else if (cm->cm_frame->dcmd.opcode == MFI_DCMD_LD_DELETE) {
		error = mfi_dcmd_command(sc, &ld_cm, MFI_DCMD_LD_GET_INFO,
		    (void **)&ld_info, sizeof(*ld_info));
		if (error) {
			device_printf(sc->mfi_dev, "Failed to allocate"
			    "MFI_DCMD_LD_GET_INFO %d", error);
			if (ld_info)
				kfree(ld_info, M_MFIBUF);
			return 0;
		}
		ld_cm->cm_flags = MFI_CMD_DATAIN;
		ld_cm->cm_frame->dcmd.mbox[0]= cm->cm_frame->dcmd.mbox[0];
		ld_cm->cm_frame->header.target_id = cm->cm_frame->dcmd.mbox[0];
		if (mfi_wait_command(sc, ld_cm) != 0) {
			device_printf(sc->mfi_dev, "failed to get log drv\n");
			mfi_release_command(ld_cm);
			kfree(ld_info, M_MFIBUF);
			return 0;
		}

		if (ld_cm->cm_frame->header.cmd_status != MFI_STAT_OK) {
			kfree(ld_info, M_MFIBUF);
			mfi_release_command(ld_cm);
			return 0;
		} else {
			ld_info = (struct mfi_ld_info *)ld_cm->cm_private;
		}

		if (ld_info->ld_config.params.isSSCD == 1)
			error = 1;

		mfi_release_command(ld_cm);
		kfree(ld_info, M_MFIBUF);
	}
	return error;
}

static int
mfi_stp_cmd(struct mfi_softc *sc, struct mfi_command *cm,caddr_t arg)
{
	uint8_t i;
	struct mfi_ioc_packet *ioc;
	ioc = (struct mfi_ioc_packet *)arg;
	int sge_size, error;
	struct megasas_sge *kern_sge;

	memset(sc->kbuff_arr, 0, sizeof(sc->kbuff_arr));
	kern_sge =(struct megasas_sge *) ((uintptr_t)cm->cm_frame + ioc->mfi_sgl_off);
	cm->cm_frame->header.sg_count = ioc->mfi_sge_count;

	if (sizeof(bus_addr_t) == 8) {
		cm->cm_frame->header.flags |= MFI_FRAME_SGL64;
		cm->cm_extra_frames = 2;
		sge_size = sizeof(struct mfi_sg64);
	} else {
		cm->cm_extra_frames =  (cm->cm_total_frame_size - 1) / MFI_FRAME_SIZE;
		sge_size = sizeof(struct mfi_sg32);
	}

	cm->cm_total_frame_size += (sge_size * ioc->mfi_sge_count);
	for (i = 0; i < ioc->mfi_sge_count; i++) {
			if (bus_dma_tag_create( sc->mfi_parent_dmat,	/* parent */
			1, 0,			/* algnmnt, boundary */
			BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
			BUS_SPACE_MAXADDR,	/* highaddr */
			ioc->mfi_sgl[i].iov_len,/* maxsize */
			2,			/* nsegments */
			ioc->mfi_sgl[i].iov_len,/* maxsegsize */
			BUS_DMA_ALLOCNOW,	/* flags */
			&sc->mfi_kbuff_arr_dmat[i])) {
			device_printf(sc->mfi_dev,
			    "Cannot allocate mfi_kbuff_arr_dmat tag\n");
			return (ENOMEM);
		}

		if (bus_dmamem_alloc(sc->mfi_kbuff_arr_dmat[i],
		    (void **)&sc->kbuff_arr[i], BUS_DMA_NOWAIT,
		    &sc->mfi_kbuff_arr_dmamap[i])) {
			device_printf(sc->mfi_dev,
			    "Cannot allocate mfi_kbuff_arr_dmamap memory\n");
			return (ENOMEM);
		}

		bus_dmamap_load(sc->mfi_kbuff_arr_dmat[i],
		    sc->mfi_kbuff_arr_dmamap[i], sc->kbuff_arr[i],
		    ioc->mfi_sgl[i].iov_len, mfi_addr_cb,
		    &sc->mfi_kbuff_arr_busaddr[i], 0);

		if (!sc->kbuff_arr[i]) {
			device_printf(sc->mfi_dev,
			    "Could not allocate memory for kbuff_arr info\n");
			return -1;
		}
		kern_sge[i].phys_addr = sc->mfi_kbuff_arr_busaddr[i];
		kern_sge[i].length = ioc->mfi_sgl[i].iov_len;

		if (sizeof(bus_addr_t) == 8) {
			cm->cm_frame->stp.sgl.sg64[i].addr =
			    kern_sge[i].phys_addr;
			cm->cm_frame->stp.sgl.sg64[i].len =
			    ioc->mfi_sgl[i].iov_len;
		} else {
			cm->cm_frame->stp.sgl.sg32[i].addr =
			    kern_sge[i].phys_addr;
			cm->cm_frame->stp.sgl.sg32[i].len =
			    ioc->mfi_sgl[i].iov_len;
		}

		error = copyin(ioc->mfi_sgl[i].iov_base,
		    sc->kbuff_arr[i],
		    ioc->mfi_sgl[i].iov_len);
		if (error != 0) {
			device_printf(sc->mfi_dev, "Copy in failed\n");
			return error;
		}
	}

	cm->cm_flags |=MFI_CMD_MAPPED;
	return 0;
}

static int
mfi_user_command(struct mfi_softc *sc, struct mfi_ioc_passthru *ioc)
{
	struct mfi_command *cm;
	struct mfi_dcmd_frame *dcmd;
	void *ioc_buf = NULL;
	uint32_t context;
	int error = 0, locked;


	if (ioc->buf_size > 0) {
		ioc_buf = kmalloc(ioc->buf_size, M_MFIBUF, M_WAITOK);
		error = copyin(ioc->buf, ioc_buf, ioc->buf_size);
		if (error) {
			device_printf(sc->mfi_dev, "failed to copyin\n");
			kfree(ioc_buf, M_MFIBUF);
			return (error);
		}
	}

	locked = mfi_config_lock(sc, ioc->ioc_frame.opcode);

	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	while ((cm = mfi_dequeue_free(sc)) == NULL)
		lksleep(mfi_user_command, &sc->mfi_io_lock, 0, "mfiioc", hz);

	/* Save context for later */
	context = cm->cm_frame->header.context;

	dcmd = &cm->cm_frame->dcmd;
	bcopy(&ioc->ioc_frame, dcmd, sizeof(struct mfi_dcmd_frame));

	cm->cm_sg = &dcmd->sgl;
	cm->cm_total_frame_size = MFI_DCMD_FRAME_SIZE;
	cm->cm_data = ioc_buf;
	cm->cm_len = ioc->buf_size;

	/* restore context */
	cm->cm_frame->header.context = context;

	/* Cheat since we don't know if we're writing or reading */
	cm->cm_flags = MFI_CMD_DATAIN | MFI_CMD_DATAOUT;

	error = mfi_check_command_pre(sc, cm);
	if (error)
		goto out;

	error = mfi_wait_command(sc, cm);
	if (error) {
		device_printf(sc->mfi_dev, "ioctl failed %d\n", error);
		goto out;
	}
	bcopy(dcmd, &ioc->ioc_frame, sizeof(struct mfi_dcmd_frame));
	mfi_check_command_post(sc, cm);
out:
	mfi_release_command(cm);
	lockmgr(&sc->mfi_io_lock, LK_RELEASE);
	mfi_config_unlock(sc, locked);
	if (ioc->buf_size > 0)
		error = copyout(ioc_buf, ioc->buf, ioc->buf_size);
	if (ioc_buf)
		kfree(ioc_buf, M_MFIBUF);
	return (error);
}

#define	PTRIN(p)		((void *)(uintptr_t)(p))

static int
mfi_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_long cmd = ap->a_cmd;
	int flag = ap->a_fflag;
	caddr_t arg = ap->a_data;
	struct mfi_softc *sc;
	union mfi_statrequest *ms;
	struct mfi_ioc_packet *ioc;
	struct mfi_ioc_aen *aen;
	struct mfi_command *cm = NULL;
	uint32_t context;
	union mfi_sense_ptr sense_ptr;
	uint8_t *data = NULL, *temp, *addr, skip_pre_post = 0;
	size_t len;
	int i, res;
	struct mfi_ioc_passthru *iop = (struct mfi_ioc_passthru *)arg;
	int error, locked;

	sc = dev->si_drv1;
	error = 0;

	if (sc->adpreset)
		return EBUSY;

	if (sc->hw_crit_error)
		return EBUSY;

	if (sc->issuepend_done == 0)
		return EBUSY;

	switch (cmd) {
	case MFIIO_STATS:
		ms = (union mfi_statrequest *)arg;
		switch (ms->ms_item) {
		case MFIQ_FREE:
		case MFIQ_BIO:
		case MFIQ_READY:
		case MFIQ_BUSY:
			bcopy(&sc->mfi_qstat[ms->ms_item], &ms->ms_qstat,
			    sizeof(struct mfi_qstat));
			break;
		default:
			error = ENOIOCTL;
			break;
		}
		break;
	case MFIIO_QUERY_DISK:
	{
		struct mfi_query_disk *qd;
		struct mfi_disk *ld;

		qd = (struct mfi_query_disk *)arg;
		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		TAILQ_FOREACH(ld, &sc->mfi_ld_tqh, ld_link) {
			if (ld->ld_id == qd->array_id)
				break;
		}
		if (ld == NULL) {
			qd->present = 0;
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			return (0);
		}
		qd->present = 1;
		if (ld->ld_flags & MFI_DISK_FLAGS_OPEN)
			qd->open = 1;
		bzero(qd->devname, SPECNAMELEN + 1);
		ksnprintf(qd->devname, SPECNAMELEN, "mfid%d", ld->ld_unit);
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		break;
	}
	case MFI_CMD:
		{
		devclass_t devclass;
		ioc = (struct mfi_ioc_packet *)arg;
		int adapter;

		adapter = ioc->mfi_adapter_no;
		if (device_get_unit(sc->mfi_dev) == 0 && adapter != 0) {
			devclass = devclass_find("mfi");
			sc = devclass_get_softc(devclass, adapter);
		}
		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		if ((cm = mfi_dequeue_free(sc)) == NULL) {
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			return (EBUSY);
		}
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		locked = 0;

		/*
		 * save off original context since copying from user
		 * will clobber some data
		 */
		context = cm->cm_frame->header.context;
		cm->cm_frame->header.context = cm->cm_index;

		bcopy(ioc->mfi_frame.raw, cm->cm_frame,
		    2 * MEGAMFI_FRAME_SIZE);
		cm->cm_total_frame_size = (sizeof(union mfi_sgl)
		    * ioc->mfi_sge_count) + ioc->mfi_sgl_off;
		cm->cm_frame->header.scsi_status = 0;
		cm->cm_frame->header.pad0 = 0;
		if (ioc->mfi_sge_count) {
			cm->cm_sg =
			    (union mfi_sgl *)&cm->cm_frame->bytes[ioc->mfi_sgl_off];
		}
		cm->cm_flags = 0;
		if (cm->cm_frame->header.flags & MFI_FRAME_DATAIN)
			cm->cm_flags |= MFI_CMD_DATAIN;
		if (cm->cm_frame->header.flags & MFI_FRAME_DATAOUT)
			cm->cm_flags |= MFI_CMD_DATAOUT;
		/* Legacy app shim */
		if (cm->cm_flags == 0)
			cm->cm_flags |= MFI_CMD_DATAIN | MFI_CMD_DATAOUT;
		cm->cm_len = cm->cm_frame->header.data_len;
		if (cm->cm_frame->header.cmd == MFI_CMD_STP) {
			cm->cm_stp_len = ioc->mfi_sgl[0].iov_len;
			cm->cm_len += cm->cm_stp_len;
		}
		if (cm->cm_len &&
		    (cm->cm_flags & (MFI_CMD_DATAIN | MFI_CMD_DATAOUT))) {
			cm->cm_data = data = kmalloc(cm->cm_len, M_MFIBUF,
			    M_WAITOK | M_ZERO);
		} else {
			cm->cm_data = 0;
		}

		/* restore header context */
		cm->cm_frame->header.context = context;

		if (cm->cm_frame->header.cmd == MFI_CMD_STP) {
			res = mfi_stp_cmd(sc, cm, arg);
			if (res != 0)
				goto out;
		} else {
			temp = data;
			if ((cm->cm_flags & MFI_CMD_DATAOUT) ||
			    (cm->cm_frame->header.cmd == MFI_CMD_STP)) {
				for (i = 0; i < ioc->mfi_sge_count; i++) {
					addr = ioc->mfi_sgl[i].iov_base;
					len = ioc->mfi_sgl[i].iov_len;
					error = copyin(addr, temp, len);
					if (error != 0) {
						device_printf(sc->mfi_dev,
						    "Copy in failed\n");
						goto out;
					}
					temp = &temp[len];
				}
			}
		}

		if (cm->cm_frame->header.cmd == MFI_CMD_DCMD)
			locked = mfi_config_lock(sc,
			     cm->cm_frame->dcmd.opcode);

		if (cm->cm_frame->header.cmd == MFI_CMD_PD_SCSI_IO) {
			cm->cm_frame->pass.sense_addr_lo =
			    (uint32_t)cm->cm_sense_busaddr;
			cm->cm_frame->pass.sense_addr_hi =
			    (uint32_t)((uint64_t)cm->cm_sense_busaddr >> 32);
		}
		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		skip_pre_post = mfi_check_for_sscd(sc, cm);
		if (!skip_pre_post) {
			error = mfi_check_command_pre(sc, cm);
			if (error) {
				lockmgr(&sc->mfi_io_lock, LK_RELEASE);
				goto out;
			}
		}

		if ((error = mfi_wait_command(sc, cm)) != 0) {
			device_printf(sc->mfi_dev,
			    "Controller polled failed\n");
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			goto out;
		}

		if (!skip_pre_post)
			mfi_check_command_post(sc, cm);
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);

		if (cm->cm_frame->header.cmd != MFI_CMD_STP) {
			temp = data;
			if ((cm->cm_flags & MFI_CMD_DATAIN) ||
			    (cm->cm_frame->header.cmd == MFI_CMD_STP)) {
				for (i = 0; i < ioc->mfi_sge_count; i++) {
					addr = ioc->mfi_sgl[i].iov_base;
					len = ioc->mfi_sgl[i].iov_len;
					error = copyout(temp, addr, len);
					if (error != 0) {
						device_printf(sc->mfi_dev,
						    "Copy out failed\n");
						goto out;
					}
					temp = &temp[len];
				}
			}
		}

		if (ioc->mfi_sense_len) {
			/* get user-space sense ptr then copy out sense */
			bcopy(&ioc->mfi_frame.raw[ioc->mfi_sense_off],
			    &sense_ptr.sense_ptr_data[0],
			    sizeof(sense_ptr.sense_ptr_data));
			error = copyout(cm->cm_sense, sense_ptr.user_space,
			    ioc->mfi_sense_len);
			if (error != 0) {
				device_printf(sc->mfi_dev,
				    "Copy out failed\n");
				goto out;
			}
		}

		ioc->mfi_frame.hdr.cmd_status = cm->cm_frame->header.cmd_status;
out:
		mfi_config_unlock(sc, locked);
		if (data)
			kfree(data, M_MFIBUF);
		if (cm->cm_frame->header.cmd == MFI_CMD_STP) {
			for (i = 0; i < 2; i++) {
				if (sc->kbuff_arr[i]) {
					if (sc->mfi_kbuff_arr_busaddr != 0)
						bus_dmamap_unload(
						    sc->mfi_kbuff_arr_dmat[i],
						    sc->mfi_kbuff_arr_dmamap[i]
						    );
					if (sc->kbuff_arr[i] != NULL)
						bus_dmamem_free(
						    sc->mfi_kbuff_arr_dmat[i],
						    sc->kbuff_arr[i],
						    sc->mfi_kbuff_arr_dmamap[i]
						    );
					if (sc->mfi_kbuff_arr_dmat[i] != NULL)
						bus_dma_tag_destroy(
						    sc->mfi_kbuff_arr_dmat[i]);
				}
			}
		}
		if (cm) {
			lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
			mfi_release_command(cm);
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		}

		break;
		}
	case MFI_SET_AEN:
		aen = (struct mfi_ioc_aen *)arg;
		error = mfi_aen_register(sc, aen->aen_seq_num,
		    aen->aen_class_locale);

		break;
	case MFI_LINUX_CMD_2: /* Firmware Linux ioctl shim */
		{
			devclass_t devclass;
			struct mfi_linux_ioc_packet l_ioc;
			int adapter;

			devclass = devclass_find("mfi");
			if (devclass == NULL)
				return (ENOENT);

			error = copyin(arg, &l_ioc, sizeof(l_ioc));
			if (error)
				return (error);
			adapter = l_ioc.lioc_adapter_no;
			sc = devclass_get_softc(devclass, adapter);
			if (sc == NULL)
				return (ENOENT);
			return (mfi_linux_ioctl_int(sc->mfi_cdev,
			    cmd, arg, flag));
			break;
		}
	case MFI_LINUX_SET_AEN_2: /* AEN Linux ioctl shim */
		{
			devclass_t devclass;
			struct mfi_linux_ioc_aen l_aen;
			int adapter;

			devclass = devclass_find("mfi");
			if (devclass == NULL)
				return (ENOENT);

			error = copyin(arg, &l_aen, sizeof(l_aen));
			if (error)
				return (error);
			adapter = l_aen.laen_adapter_no;
			sc = devclass_get_softc(devclass, adapter);
			if (sc == NULL)
				return (ENOENT);
			return (mfi_linux_ioctl_int(sc->mfi_cdev,
			    cmd, arg, flag));
			break;
		}
	case MFIIO_PASSTHRU:
		error = mfi_user_command(sc, iop);
		break;
	default:
		device_printf(sc->mfi_dev, "IOCTL 0x%lx not handled\n", cmd);
		error = ENOENT;
		break;
	}

	return (error);
}

static int
mfi_linux_ioctl_int(struct cdev *dev, u_long cmd, caddr_t arg, int flag)
{
	struct mfi_softc *sc;
	struct mfi_linux_ioc_packet l_ioc;
	struct mfi_linux_ioc_aen l_aen;
	struct mfi_command *cm = NULL;
	struct mfi_aen *mfi_aen_entry;
	union mfi_sense_ptr sense_ptr;
	uint32_t context;
	uint8_t *data = NULL, *temp;
	int i;
	int error, locked;

	sc = dev->si_drv1;
	error = 0;
	switch (cmd) {
	case MFI_LINUX_CMD_2: /* Firmware Linux ioctl shim */
		error = copyin(arg, &l_ioc, sizeof(l_ioc));
		if (error != 0)
			return (error);

		if (l_ioc.lioc_sge_count > MAX_LINUX_IOCTL_SGE) {
			return (EINVAL);
		}

		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		if ((cm = mfi_dequeue_free(sc)) == NULL) {
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			return (EBUSY);
		}
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		locked = 0;

		/*
		 * save off original context since copying from user
		 * will clobber some data
		 */
		context = cm->cm_frame->header.context;

		bcopy(l_ioc.lioc_frame.raw, cm->cm_frame,
		      2 * MFI_DCMD_FRAME_SIZE);	/* this isn't quite right */
		cm->cm_total_frame_size = (sizeof(union mfi_sgl)
		      * l_ioc.lioc_sge_count) + l_ioc.lioc_sgl_off;
		cm->cm_frame->header.scsi_status = 0;
		cm->cm_frame->header.pad0 = 0;
		if (l_ioc.lioc_sge_count)
			cm->cm_sg =
			    (union mfi_sgl *)&cm->cm_frame->bytes[l_ioc.lioc_sgl_off];
		cm->cm_flags = 0;
		if (cm->cm_frame->header.flags & MFI_FRAME_DATAIN)
			cm->cm_flags |= MFI_CMD_DATAIN;
		if (cm->cm_frame->header.flags & MFI_FRAME_DATAOUT)
			cm->cm_flags |= MFI_CMD_DATAOUT;
		cm->cm_len = cm->cm_frame->header.data_len;
		if (cm->cm_len &&
		      (cm->cm_flags & (MFI_CMD_DATAIN | MFI_CMD_DATAOUT))) {
			cm->cm_data = data = kmalloc(cm->cm_len, M_MFIBUF,
			    M_WAITOK | M_ZERO);
		} else {
			cm->cm_data = 0;
		}

		/* restore header context */
		cm->cm_frame->header.context = context;

		temp = data;
		if (cm->cm_flags & MFI_CMD_DATAOUT) {
			for (i = 0; i < l_ioc.lioc_sge_count; i++) {
				error = copyin(PTRIN(l_ioc.lioc_sgl[i].iov_base),
				       temp,
				       l_ioc.lioc_sgl[i].iov_len);
				if (error != 0) {
					device_printf(sc->mfi_dev,
					    "Copy in failed\n");
					goto out;
				}
				temp = &temp[l_ioc.lioc_sgl[i].iov_len];
			}
		}

		if (cm->cm_frame->header.cmd == MFI_CMD_DCMD)
			locked = mfi_config_lock(sc, cm->cm_frame->dcmd.opcode);

		if (cm->cm_frame->header.cmd == MFI_CMD_PD_SCSI_IO) {
			cm->cm_frame->pass.sense_addr_lo =
			    (uint32_t)cm->cm_sense_busaddr;
			cm->cm_frame->pass.sense_addr_hi =
			    (uint32_t)((uint64_t)cm->cm_sense_busaddr >> 32);
		}

		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		error = mfi_check_command_pre(sc, cm);
		if (error) {
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			goto out;
		}

		if ((error = mfi_wait_command(sc, cm)) != 0) {
			device_printf(sc->mfi_dev,
			    "Controller polled failed\n");
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
			goto out;
		}

		mfi_check_command_post(sc, cm);
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);

		temp = data;
		if (cm->cm_flags & MFI_CMD_DATAIN) {
			for (i = 0; i < l_ioc.lioc_sge_count; i++) {
				error = copyout(temp,
					PTRIN(l_ioc.lioc_sgl[i].iov_base),
					l_ioc.lioc_sgl[i].iov_len);
				if (error != 0) {
					device_printf(sc->mfi_dev,
					    "Copy out failed\n");
					goto out;
				}
				temp = &temp[l_ioc.lioc_sgl[i].iov_len];
			}
		}

		if (l_ioc.lioc_sense_len) {
			/* get user-space sense ptr then copy out sense */
			bcopy(&((struct mfi_linux_ioc_packet*)arg)
                            ->lioc_frame.raw[l_ioc.lioc_sense_off],
			    &sense_ptr.sense_ptr_data[0],
			    sizeof(sense_ptr.sense_ptr_data));
#ifdef __x86_64__
			/*
			 * only 32bit Linux support so zero out any
			 * address over 32bit
			 */
			sense_ptr.addr.high = 0;
#endif
			error = copyout(cm->cm_sense, sense_ptr.user_space,
			    l_ioc.lioc_sense_len);
			if (error != 0) {
				device_printf(sc->mfi_dev,
				    "Copy out failed\n");
				goto out;
			}
		}

		error = copyout(&cm->cm_frame->header.cmd_status,
			&((struct mfi_linux_ioc_packet*)arg)
			->lioc_frame.hdr.cmd_status,
			1);
		if (error != 0) {
			device_printf(sc->mfi_dev,
				      "Copy out failed\n");
			goto out;
		}

out:
		mfi_config_unlock(sc, locked);
		if (data)
			kfree(data, M_MFIBUF);
		if (cm) {
			lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
			mfi_release_command(cm);
			lockmgr(&sc->mfi_io_lock, LK_RELEASE);
		}

		return (error);
	case MFI_LINUX_SET_AEN_2: /* AEN Linux ioctl shim */
		error = copyin(arg, &l_aen, sizeof(l_aen));
		if (error != 0)
			return (error);
		kprintf("AEN IMPLEMENTED for pid %d\n", curproc->p_pid);
		mfi_aen_entry = kmalloc(sizeof(struct mfi_aen), M_MFIBUF,
		    M_WAITOK);
		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		if (mfi_aen_entry != NULL) {
			mfi_aen_entry->p = curproc;
			TAILQ_INSERT_TAIL(&sc->mfi_aen_pids, mfi_aen_entry,
			    aen_link);
		}
		error = mfi_aen_register(sc, l_aen.laen_seq_num,
		    l_aen.laen_class_locale);

		if (error != 0) {
			TAILQ_REMOVE(&sc->mfi_aen_pids, mfi_aen_entry,
			    aen_link);
			kfree(mfi_aen_entry, M_MFIBUF);
		}
		lockmgr(&sc->mfi_io_lock, LK_RELEASE);

		return (error);
	default:
		device_printf(sc->mfi_dev, "IOCTL 0x%lx not handled\n", cmd);
		error = ENOENT;
		break;
	}

	return (error);
}

static int
mfi_kqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct mfi_softc *sc;
	struct klist *klist;

	ap->a_result = 0;
	sc = dev->si_drv1;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &mfi_read_filterops;
		kn->kn_hook = (caddr_t)sc;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &mfi_write_filterops;
		kn->kn_hook = (caddr_t)sc;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	klist = &sc->mfi_kq.ki_note;
	knote_insert(klist, kn);

	return(0);
}

static void
mfi_filter_detach(struct knote *kn)
{
	struct mfi_softc *sc = (struct mfi_softc *)kn->kn_hook;
	struct klist *klist = &sc->mfi_kq.ki_note;

	knote_remove(klist, kn);
}

static int
mfi_filter_read(struct knote *kn, long hint)
{
	struct mfi_softc *sc = (struct mfi_softc *)kn->kn_hook;
	int ready = 0;

	if (sc->mfi_aen_triggered != 0) {
		ready = 1;
		sc->mfi_aen_triggered = 0;
	}
	if (sc->mfi_aen_triggered == 0 && sc->mfi_aen_cm == NULL)
		kn->kn_flags |= EV_ERROR;

	if (ready == 0)
		sc->mfi_poll_waiting = 1;

	return (ready);
}

static int
mfi_filter_write(struct knote *kn, long hint)
{
	return (0);
}

static void
mfi_dump_all(void)
{
	struct mfi_softc *sc;
	struct mfi_command *cm;
	devclass_t dc;
	time_t deadline;
	int timedout;
	int i;

	dc = devclass_find("mfi");
	if (dc == NULL) {
		kprintf("No mfi dev class\n");
		return;
	}

	for (i = 0; ; i++) {
		sc = devclass_get_softc(dc, i);
		if (sc == NULL)
			break;
		device_printf(sc->mfi_dev, "Dumping\n\n");
		timedout = 0;
		deadline = time_uptime - mfi_cmd_timeout;
		lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
		TAILQ_FOREACH(cm, &sc->mfi_busy, cm_link) {
			if (cm->cm_timestamp < deadline) {
				device_printf(sc->mfi_dev,
				    "COMMAND %p TIMEOUT AFTER %d SECONDS\n",
				    cm, (int)(time_uptime - cm->cm_timestamp));
				MFI_PRINT_CMD(cm);
				timedout++;
			}
		}

#if 0
		if (timedout)
			MFI_DUMP_CMDS(SC);
#endif

		lockmgr(&sc->mfi_io_lock, LK_RELEASE);
	}

	return;
}

static void
mfi_timeout(void *data)
{
	struct mfi_softc *sc = (struct mfi_softc *)data;
	struct mfi_command *cm;
	time_t deadline;
	int timedout = 0;

	deadline = time_uptime - mfi_cmd_timeout;
	if (sc->adpreset == 0) {
		if (!mfi_tbolt_reset(sc)) {
			callout_reset(&sc->mfi_watchdog_callout,
			    mfi_cmd_timeout * hz, mfi_timeout, sc);
			return;
		}
	}
	lockmgr(&sc->mfi_io_lock, LK_EXCLUSIVE);
	TAILQ_FOREACH(cm, &sc->mfi_busy, cm_link) {
		if (sc->mfi_aen_cm == cm)
			continue;
		if ((sc->mfi_aen_cm != cm) && (cm->cm_timestamp < deadline)) {
			if (sc->adpreset != 0 && sc->issuepend_done == 0) {
				cm->cm_timestamp = time_uptime;
			} else {
				device_printf(sc->mfi_dev,
				    "COMMAND %p TIMEOUT AFTER %d SECONDS\n",
				     cm, (int)(time_uptime - cm->cm_timestamp));
				MFI_PRINT_CMD(cm);
				MFI_VALIDATE_CMD(sc, cm);
				timedout++;
			}
		}
	}

#if 0
	if (timedout)
		MFI_DUMP_CMDS(SC);
#endif

	lockmgr(&sc->mfi_io_lock, LK_RELEASE);

	callout_reset(&sc->mfi_watchdog_callout, mfi_cmd_timeout * hz,
	    mfi_timeout, sc);

	if (0)
		mfi_dump_all();
	return;
}
