/*-
 * Copyright (c) 2009-2012,2016 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/systimer.h>
#include <sys/thread.h>
#include <sys/thread2.h>

#include <machine/intr_machdep.h>
#include <machine/smp.h>

#include <dev/virtual/hyperv/hyperv_busdma.h>
#include <dev/virtual/hyperv/hyperv_machdep.h>
#include <dev/virtual/hyperv/hyperv_reg.h>
#include <dev/virtual/hyperv/hyperv_var.h>
#include <dev/virtual/hyperv/vmbus/vmbus_reg.h>
#include <dev/virtual/hyperv/vmbus/vmbus_var.h>

#include "acpi.h"
#include "acpi_if.h"
#include "pcib_if.h"

#define MSR_HV_STIMER0_CFG_SINT		\
	((((uint64_t)VMBUS_SINT_TIMER) << MSR_HV_STIMER_CFG_SINT_SHIFT) & \
	 MSR_HV_STIMER_CFG_SINT_MASK)

/*
 * Additionally required feature:
 * - SynIC is needed for interrupt generation.
 */
#define CPUID_HV_TIMER_MASK		(CPUID_HV_MSR_SYNIC |		\
					 CPUID_HV_MSR_SYNTIMER)

/*
 * NOTE: DO NOT CHANGE THIS.
 */
#define VMBUS_SINT_MESSAGE		2
/*
 * NOTE:
 * - DO NOT set it to the same value as VMBUS_SINT_MESSAGE.
 * - DO NOT set it to 0.
 */
#define VMBUS_SINT_TIMER		4

/*
 * NOTE: DO NOT CHANGE THESE
 */
#define VMBUS_CONNID_MESSAGE		1
#define VMBUS_CONNID_EVENT		2

struct vmbus_msghc {
	struct hypercall_postmsg_in	*mh_inprm;
	struct hypercall_postmsg_in	mh_inprm_save;
	struct hyperv_dma		mh_inprm_dma;

	struct vmbus_message		*mh_resp;
	struct vmbus_message		mh_resp0;
};

struct vmbus_msghc_ctx {
	struct vmbus_msghc		*mhc_free;
	struct lwkt_token		mhc_free_token;
	uint32_t			mhc_flags;

	struct vmbus_msghc		*mhc_active;
	struct lwkt_token		mhc_active_token;
};

#define VMBUS_MSGHC_CTXF_DESTROY	0x0001

static int			vmbus_probe(device_t);
static int			vmbus_attach(device_t);
static int			vmbus_detach(device_t);
static void			vmbus_intr(void *);
static void			vmbus_timer_intr_reload(struct cputimer_intr *,
				    sysclock_t);
static void			vmbus_timer_intr_pcpuhand(
				    struct cputimer_intr *);
static void			vmbus_timer_intr_restart(
				    struct cputimer_intr *);

static int			vmbus_dma_alloc(struct vmbus_softc *);
static void			vmbus_dma_free(struct vmbus_softc *);
static int			vmbus_intr_setup(struct vmbus_softc *);
static void			vmbus_intr_teardown(struct vmbus_softc *);
static void			vmbus_synic_setup(void *);
static void			vmbus_synic_teardown(void *);
static void			vmbus_timer_stop(void *);
static void			vmbus_timer_config(void *);
static int			vmbus_init(struct vmbus_softc *);
static int			vmbus_init_contact(struct vmbus_softc *,
				    uint32_t);
static void			vmbus_timer_restart(void *);
static void			vmbus_timer_msgintr(struct vmbus_pcpu_data *);

static void			vmbus_chan_msgproc(struct vmbus_softc *,
				    const struct vmbus_message *);

static struct vmbus_msghc_ctx	*vmbus_msghc_ctx_create(bus_dma_tag_t);
static void			vmbus_msghc_ctx_destroy(
				    struct vmbus_msghc_ctx *);
static void			vmbus_msghc_ctx_free(struct vmbus_msghc_ctx *);
static struct vmbus_msghc	*vmbus_msghc_alloc(bus_dma_tag_t);
static void			vmbus_msghc_free(struct vmbus_msghc *);
static struct vmbus_msghc	*vmbus_msghc_get1(struct vmbus_msghc_ctx *,
				    uint32_t);

static device_method_t vmbus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			vmbus_probe),
	DEVMETHOD(device_attach,		vmbus_attach),
	DEVMETHOD(device_detach,		vmbus_detach),
	DEVMETHOD(device_shutdown,		bus_generic_shutdown),
	DEVMETHOD(device_suspend,		bus_generic_suspend),
	DEVMETHOD(device_resume,		bus_generic_resume),

	DEVMETHOD_END
};

static driver_t vmbus_driver = {
	"vmbus",
	vmbus_methods,
	sizeof(struct vmbus_softc)
};

static devclass_t vmbus_devclass;

DRIVER_MODULE(vmbus, acpi, vmbus_driver, vmbus_devclass, NULL, NULL);
MODULE_DEPEND(vmbus, acpi, 1, 1, 1);
MODULE_VERSION(vmbus, 1);

static struct cputimer_intr vmbus_cputimer_intr = {
	.freq = HYPERV_TIMER_FREQ,
	.reload = vmbus_timer_intr_reload,
	.enable = cputimer_intr_default_enable,
	.config = cputimer_intr_default_config,
	.restart = vmbus_timer_intr_restart,
	.pmfixup = cputimer_intr_default_pmfixup,
	.initclock = cputimer_intr_default_initclock,
	.pcpuhand = vmbus_timer_intr_pcpuhand,
	.next = SLIST_ENTRY_INITIALIZER,
	.name = "hyperv",
	.type = CPUTIMER_INTR_VMM,
	.prio = CPUTIMER_INTR_PRIO_VMM,
	.caps = CPUTIMER_INTR_CAP_PS,
	.priv = NULL
};

static const uint32_t	vmbus_version[] = {
	VMBUS_VERSION_WIN8_1,
	VMBUS_VERSION_WIN8,
	VMBUS_VERSION_WIN7,
	VMBUS_VERSION_WS2008
};

static int		vmbus_timer_intr_enable = 1;
TUNABLE_INT("hw.vmbus.timer_intr.enable", &vmbus_timer_intr_enable);

static int
vmbus_probe(device_t dev)
{
	char *id[] = { "VMBUS", NULL };

	if (ACPI_ID_PROBE(device_get_parent(dev), dev, id) == NULL ||
	    device_get_unit(dev) != 0 || vmm_guest != VMM_GUEST_HYPERV ||
	    (hyperv_features & CPUID_HV_MSR_SYNIC) == 0)
		return (ENXIO);

	device_set_desc(dev, "Hyper-V vmbus");

	return (0);
}

static int
vmbus_attach(device_t dev)
{
	struct vmbus_softc *sc = device_get_softc(dev);
	int error, cpu, use_timer;

	/*
	 * Basic setup.
	 */
	sc->vmbus_dev = dev;
	for (cpu = 0; cpu < ncpus; ++cpu) {
		struct vmbus_pcpu_data *psc = VMBUS_PCPU(sc, cpu);

		psc->sc = sc;
		psc->cpuid = cpu;
		psc->timer_last = UINT64_MAX;
	}

	/*
	 * Should we use interrupt timer?
	 */
	use_timer = 0;
	if (device_get_unit(dev) == 0 &&
	    (hyperv_features & CPUID_HV_TIMER_MASK) == CPUID_HV_TIMER_MASK &&
	    hyperv_tc64 != NULL)
		use_timer = 1;

	/*
	 * Create context for "post message" Hypercalls
	 */
	sc->vmbus_msg_hc = vmbus_msghc_ctx_create(
	    bus_get_dma_tag(sc->vmbus_dev));
	if (sc->vmbus_msg_hc == NULL)
		return ENXIO;

	/*
	 * Allocate DMA stuffs.
	 */
	error = vmbus_dma_alloc(sc);
	if (error)
		goto failed;

	/*
	 * Setup interrupt.
	 */
	error = vmbus_intr_setup(sc);
	if (error)
		goto failed;

	if (use_timer) {
		/*
		 * Make sure that interrupt timer is stopped.
		 */
		lwkt_cpusync_simple(smp_active_mask, vmbus_timer_stop, sc);
	}

	/*
	 * Setup SynIC.
	 */
	lwkt_cpusync_simple(smp_active_mask, vmbus_synic_setup, sc);
	sc->vmbus_flags |= VMBUS_FLAG_SYNIC;

	/*
	 * Initialize vmbus.
	 */
	error = vmbus_init(sc);
	if (error)
		goto failed;

	if (use_timer) {
		/*
		 * Configure and register vmbus interrupt timer.
		 */
		lwkt_cpusync_simple(smp_active_mask, vmbus_timer_config, sc);
		vmbus_cputimer_intr.priv = sc;
		cputimer_intr_register(&vmbus_cputimer_intr);
		if (vmbus_timer_intr_enable)
			cputimer_intr_select(&vmbus_cputimer_intr, 0);
	}

	return 0;
failed:
	vmbus_detach(dev);
	return error;
}

static int
vmbus_detach(device_t dev)
{
	struct vmbus_softc *sc = device_get_softc(dev);

	/* TODO: uninitialize vmbus. */
	/* TODO: stop and deregister timer */

	if (sc->vmbus_flags & VMBUS_FLAG_SYNIC)
		lwkt_cpusync_simple(smp_active_mask, vmbus_synic_teardown, sc);
	vmbus_intr_teardown(sc);
	vmbus_dma_free(sc);

	if (sc->vmbus_msg_hc != NULL) {
		vmbus_msghc_ctx_destroy(sc->vmbus_msg_hc);
		sc->vmbus_msg_hc = NULL;
	}
	return (0);
}

static __inline void
vmbus_msg_reset(volatile struct vmbus_message *msg)
{
	msg->msg_type = HYPERV_MSGTYPE_NONE;
	/*
	 * Make sure that the write to msg_type (i.e. set to
	 * HYPERV_MSGTYPE_NONE) happens before we read the
	 * msg_flags and send EOM to the hypervisor.
	 */
	cpu_mfence();
	if (msg->msg_flags & VMBUS_MSGFLAG_PENDING) {
		/*
		 * Ask the hypervisor to rescan message queue,
		 * and deliver new message if any.
		 */
		wrmsr(MSR_HV_EOM, 0);
	}
}

static void
vmbus_intr(void *xpsc)
{
	struct vmbus_pcpu_data *psc = xpsc;
	volatile struct vmbus_message *msg;

	msg = psc->message + VMBUS_SINT_MESSAGE;
	while (__predict_false(msg->msg_type != HYPERV_MSGTYPE_NONE)) {
		if (msg->msg_type == HYPERV_MSGTYPE_CHANNEL) {
			/* Channel message */
			vmbus_chan_msgproc(psc->sc,
			    __DEVOLATILE(const struct vmbus_message *, msg));
		}
		vmbus_msg_reset(msg);
	}
}

static __inline void
vmbus_timer_oneshot(struct vmbus_pcpu_data *psc, uint64_t current)
{
	psc->timer_last = current;
	wrmsr(MSR_HV_STIMER0_COUNT, current);
}

static void
vmbus_timer_intr_reload(struct cputimer_intr *cti, sysclock_t reload)
{
	struct globaldata *gd = mycpu;
	struct vmbus_softc *sc = cti->priv;
	struct vmbus_pcpu_data *psc = VMBUS_PCPU(sc, gd->gd_cpuid);
	uint64_t current;

	if ((ssysclock_t)reload < 0)		/* neg value */
		reload = 1;
	reload = muldivu64(reload, cti->freq, sys_cputimer->freq);
	current = hyperv_tc64() + reload;

	if (gd->gd_timer_running) {
		if (current < psc->timer_last)
			vmbus_timer_oneshot(psc, current);
	} else {
		gd->gd_timer_running = 1;
		vmbus_timer_oneshot(psc, current);
	}
}

static void
vmbus_timer_intr_pcpuhand(struct cputimer_intr *cti)
{
	struct vmbus_softc *sc = cti->priv;
	struct vmbus_pcpu_data *psc = VMBUS_PCPU(sc, mycpuid);

	vmbus_timer_msgintr(psc);
}

static void
vmbus_timer_intr_restart(struct cputimer_intr *cti)
{
	lwkt_send_ipiq_mask(smp_active_mask, vmbus_timer_restart, cti->priv);
}

static struct vmbus_msghc *
vmbus_msghc_alloc(bus_dma_tag_t parent_dtag)
{
	struct vmbus_msghc *mh;

	mh = kmalloc(sizeof(*mh), M_DEVBUF, M_WAITOK | M_ZERO);

	mh->mh_inprm = hyperv_dmamem_alloc(parent_dtag,
	    HYPERCALL_POSTMSGIN_ALIGN, 0, HYPERCALL_POSTMSGIN_SIZE,
	    &mh->mh_inprm_dma, BUS_DMA_WAITOK);
	if (mh->mh_inprm == NULL) {
		kfree(mh, M_DEVBUF);
		return NULL;
	}
	return mh;
}

static void
vmbus_msghc_free(struct vmbus_msghc *mh)
{
	hyperv_dmamem_free(&mh->mh_inprm_dma, mh->mh_inprm);
	kfree(mh, M_DEVBUF);
}

static void
vmbus_msghc_ctx_free(struct vmbus_msghc_ctx *mhc)
{
	KASSERT(mhc->mhc_active == NULL, ("still have active msg hypercall"));
	KASSERT(mhc->mhc_free == NULL, ("still have hypercall msg"));

	lwkt_token_uninit(&mhc->mhc_free_token);
	lwkt_token_uninit(&mhc->mhc_active_token);
	kfree(mhc, M_DEVBUF);
}

static struct vmbus_msghc_ctx *
vmbus_msghc_ctx_create(bus_dma_tag_t parent_dtag)
{
	struct vmbus_msghc_ctx *mhc;

	mhc = kmalloc(sizeof(*mhc), M_DEVBUF, M_WAITOK | M_ZERO);
	lwkt_token_init(&mhc->mhc_free_token, "msghcf");
	lwkt_token_init(&mhc->mhc_active_token, "msghca");

	mhc->mhc_free = vmbus_msghc_alloc(parent_dtag);
	if (mhc->mhc_free == NULL) {
		vmbus_msghc_ctx_free(mhc);
		return NULL;
	}
	return mhc;
}

static struct vmbus_msghc *
vmbus_msghc_get1(struct vmbus_msghc_ctx *mhc, uint32_t dtor_flag)
{
	struct vmbus_msghc *mh;

	lwkt_gettoken(&mhc->mhc_free_token);

	while ((mhc->mhc_flags & dtor_flag) == 0 && mhc->mhc_free == NULL)
		tsleep(&mhc->mhc_free, 0, "gmsghc", 0);
	if (mhc->mhc_flags & dtor_flag) {
		/* Being destroyed */
		mh = NULL;
	} else {
		mh = mhc->mhc_free;
		KASSERT(mh != NULL, ("no free hypercall msg"));
		KASSERT(mh->mh_resp == NULL,
		    ("hypercall msg has pending response"));
		mhc->mhc_free = NULL;
	}

	lwkt_reltoken(&mhc->mhc_free_token);

	return mh;
}

struct vmbus_msghc *
vmbus_msghc_get(struct vmbus_softc *sc, size_t dsize)
{
	struct hypercall_postmsg_in *inprm;
	struct vmbus_msghc *mh;

	if (dsize > HYPERCALL_POSTMSGIN_DSIZE_MAX)
		return NULL;

	mh = vmbus_msghc_get1(sc->vmbus_msg_hc, VMBUS_MSGHC_CTXF_DESTROY);
	if (mh == NULL)
		return NULL;

	inprm = mh->mh_inprm;
	memset(inprm, 0, HYPERCALL_POSTMSGIN_SIZE);
	inprm->hc_connid = VMBUS_CONNID_MESSAGE;
	inprm->hc_msgtype = HYPERV_MSGTYPE_CHANNEL;
	inprm->hc_dsize = dsize;

	return mh;
}

void
vmbus_msghc_put(struct vmbus_softc *sc, struct vmbus_msghc *mh)
{
	struct vmbus_msghc_ctx *mhc = sc->vmbus_msg_hc;

	KASSERT(mhc->mhc_active == NULL, ("msg hypercall is active"));
	mh->mh_resp = NULL;

	lwkt_gettoken(&mhc->mhc_free_token);
	KASSERT(mhc->mhc_free == NULL, ("has free hypercall msg"));
	mhc->mhc_free = mh;
	lwkt_reltoken(&mhc->mhc_free_token);
	wakeup(&mhc->mhc_free);
}

void *
vmbus_msghc_dataptr(struct vmbus_msghc *mh)
{
	return mh->mh_inprm->hc_data;
}

static void
vmbus_msghc_ctx_destroy(struct vmbus_msghc_ctx *mhc)
{
	struct vmbus_msghc *mh;

	lwkt_gettoken(&mhc->mhc_free_token);
	mhc->mhc_flags |= VMBUS_MSGHC_CTXF_DESTROY;
	lwkt_reltoken(&mhc->mhc_free_token);
	wakeup(&mhc->mhc_free);

	mh = vmbus_msghc_get1(mhc, 0);
	if (mh == NULL)
		panic("can't get msghc");

	vmbus_msghc_free(mh);
	vmbus_msghc_ctx_free(mhc);
}

int
vmbus_msghc_exec_noresult(struct vmbus_msghc *mh)
{
	int i, wait_ticks = 1;

	/*
	 * Save the input parameter so that we could restore the input
	 * parameter if the Hypercall failed.
	 *
	 * XXX
	 * Is this really necessary?!  i.e. Will the Hypercall ever
	 * overwrite the input parameter?
	 */
	memcpy(&mh->mh_inprm_save, mh->mh_inprm, HYPERCALL_POSTMSGIN_SIZE);

	/*
	 * In order to cope with transient failures, e.g. insufficient
	 * resources on host side, we retry the post message Hypercall
	 * several times.  20 retries seem sufficient.
	 */
#define HC_RETRY_MAX	20

	for (i = 0; i < HC_RETRY_MAX; ++i) {
		uint64_t status;

		status = hypercall_post_message(mh->mh_inprm_dma.hv_paddr);
		if (status == HYPERCALL_STATUS_SUCCESS)
			return 0;

		tsleep(&status, 0, "hcpmsg", wait_ticks);
		if (wait_ticks < hz)
			wait_ticks *= 2;

		/* Restore input parameter and try again */
		memcpy(mh->mh_inprm, &mh->mh_inprm_save,
		    HYPERCALL_POSTMSGIN_SIZE);
	}

#undef HC_RETRY_MAX

	return EIO;
}

int
vmbus_msghc_exec(struct vmbus_softc *sc, struct vmbus_msghc *mh)
{
	struct vmbus_msghc_ctx *mhc = sc->vmbus_msg_hc;
	int error;

	KASSERT(mh->mh_resp == NULL, ("hypercall msg has pending response"));

	lwkt_gettoken(&mhc->mhc_active_token);
	KASSERT(mhc->mhc_active == NULL, ("pending active msg hypercall"));
	mhc->mhc_active = mh;
	lwkt_reltoken(&mhc->mhc_active_token);

	error = vmbus_msghc_exec_noresult(mh);
	if (error) {
		lwkt_gettoken(&mhc->mhc_active_token);
		KASSERT(mhc->mhc_active == mh, ("msghc mismatch"));
		mhc->mhc_active = NULL;
		lwkt_reltoken(&mhc->mhc_active_token);
	}
	return error;
}

const struct vmbus_message *
vmbus_msghc_wait_result(struct vmbus_softc *sc, struct vmbus_msghc *mh)
{
	struct vmbus_msghc_ctx *mhc = sc->vmbus_msg_hc;

	lwkt_gettoken(&mhc->mhc_active_token);

	KASSERT(mhc->mhc_active == mh, ("msghc mismatch"));
	while (mh->mh_resp == NULL)
		tsleep(&mhc->mhc_active, 0, "wmsghc", 0);
	mhc->mhc_active = NULL;

	lwkt_reltoken(&mhc->mhc_active_token);

	return mh->mh_resp;
}

void
vmbus_msghc_wakeup(struct vmbus_softc *sc, const struct vmbus_message *msg)
{
	struct vmbus_msghc_ctx *mhc = sc->vmbus_msg_hc;
	struct vmbus_msghc *mh;

	lwkt_gettoken(&mhc->mhc_active_token);

	mh = mhc->mhc_active;
	KASSERT(mh != NULL, ("no pending msg hypercall"));
	memcpy(&mh->mh_resp0, msg, sizeof(mh->mh_resp0));
	mh->mh_resp = &mh->mh_resp0;

	lwkt_reltoken(&mhc->mhc_active_token);
	wakeup(&mhc->mhc_active);
}

static int
vmbus_dma_alloc(struct vmbus_softc *sc)
{
	bus_dma_tag_t parent_dtag;
	uint8_t *evtflags;
	int cpu;

	parent_dtag = bus_get_dma_tag(sc->vmbus_dev);
	for (cpu = 0; cpu < ncpus; ++cpu) {
		struct vmbus_pcpu_data *psc = VMBUS_PCPU(sc, cpu);

		/*
		 * Per-cpu messages and event flags.
		 */
		psc->message = hyperv_dmamem_alloc(parent_dtag,
		    PAGE_SIZE, 0, PAGE_SIZE, &psc->message_dma,
		    BUS_DMA_WAITOK | BUS_DMA_ZERO);
		if (psc->message == NULL)
			return ENOMEM;

		psc->event_flags = hyperv_dmamem_alloc(parent_dtag,
		    PAGE_SIZE, 0, PAGE_SIZE, &psc->event_flags_dma,
		    BUS_DMA_WAITOK | BUS_DMA_ZERO);
		if (psc->event_flags == NULL)
			return ENOMEM;
	}

	evtflags = hyperv_dmamem_alloc(parent_dtag, PAGE_SIZE, 0,
	    PAGE_SIZE, &sc->vmbus_evtflags_dma, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (evtflags == NULL)
		return ENOMEM;
	sc->vmbus_rx_evtflags = (u_long *)evtflags;
	sc->vmbus_tx_evtflags = (u_long *)(evtflags + (PAGE_SIZE / 2));
	sc->vmbus_evtflags = evtflags;

	sc->vmbus_mnf1 = hyperv_dmamem_alloc(parent_dtag, PAGE_SIZE, 0,
	    PAGE_SIZE, &sc->vmbus_mnf1_dma, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (sc->vmbus_mnf1 == NULL)
		return ENOMEM;

	sc->vmbus_mnf2 = hyperv_dmamem_alloc(parent_dtag, PAGE_SIZE, 0,
	    PAGE_SIZE, &sc->vmbus_mnf2_dma, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (sc->vmbus_mnf2 == NULL)
		return ENOMEM;

	return 0;
}

static void
vmbus_dma_free(struct vmbus_softc *sc)
{
	int cpu;

	if (sc->vmbus_evtflags != NULL) {
		hyperv_dmamem_free(&sc->vmbus_evtflags_dma, sc->vmbus_evtflags);
		sc->vmbus_evtflags = NULL;
		sc->vmbus_rx_evtflags = NULL;
		sc->vmbus_tx_evtflags = NULL;
	}
	if (sc->vmbus_mnf1 != NULL) {
		hyperv_dmamem_free(&sc->vmbus_mnf1_dma, sc->vmbus_mnf1);
		sc->vmbus_mnf1 = NULL;
	}
	if (sc->vmbus_mnf2 != NULL) {
		hyperv_dmamem_free(&sc->vmbus_mnf2_dma, sc->vmbus_mnf2);
		sc->vmbus_mnf2 = NULL;
	}

	for (cpu = 0; cpu < ncpus; ++cpu) {
		struct vmbus_pcpu_data *psc = VMBUS_PCPU(sc, cpu);

		if (psc->message != NULL) {
			hyperv_dmamem_free(&psc->message_dma, psc->message);
			psc->message = NULL;
		}
		if (psc->event_flags != NULL) {
			hyperv_dmamem_free(&psc->event_flags_dma,
			    psc->event_flags);
			psc->event_flags = NULL;
		}
	}
}

static int
vmbus_intr_setup(struct vmbus_softc *sc)
{
	device_t dev = sc->vmbus_dev;
	device_t bus = device_get_parent(device_get_parent(dev));
	int rid, cpu;

	rid = 0;
	for (cpu = 0; cpu < ncpus; ++cpu) {
		struct vmbus_pcpu_data *psc = VMBUS_PCPU(sc, cpu);
		uint64_t msi_addr;
		uint32_t msi_data;
		int error;

		error = PCIB_ALLOC_MSIX(bus, dev, &psc->intr_irq, cpu);
		if (error) {
			device_printf(dev, "alloc vector on cpu%d failed: %d\n",
			    cpu, error);
			return ENXIO;
		}
		psc->intr_rid = ++rid;

		psc->intr_res = BUS_ALLOC_RESOURCE(bus, dev, SYS_RES_IRQ,
		    &psc->intr_rid, psc->intr_irq, psc->intr_irq, 1,
		    RF_ACTIVE, cpu);
		if (psc->intr_res == NULL) {
			device_printf(dev, "alloc irq on cpu%d failed: %d\n",
			    cpu, error);
			return ENXIO;
		}

		error = PCIB_MAP_MSI(bus, dev, rman_get_start(psc->intr_res),
		    &msi_addr, &msi_data, cpu);
		if (error) {
			device_printf(dev, "map irq on cpu%d failed: %d\n",
			    cpu, error);
			return ENXIO;
		}
		psc->intr_vec = hyperv_msi2vector(msi_addr, msi_data);

		if (bootverbose) {
			device_printf(dev, "vector %d irq %d on cpu%d\n",
			    psc->intr_vec, psc->intr_irq, cpu);
		}

		ksnprintf(psc->intr_desc, sizeof(psc->intr_desc), "%s cpu%d",
		    device_get_nameunit(dev), cpu);
		error = bus_setup_intr_descr(dev, psc->intr_res, INTR_MPSAFE,
		    vmbus_intr, psc, &psc->intr_hand, NULL, psc->intr_desc);
		if (error) {
			device_printf(dev, "setup intr on cpu%d failed: %d\n",
			    cpu, error);
			return ENXIO;
		}
	}
	return 0;
}

static void
vmbus_intr_teardown(struct vmbus_softc *sc)
{
	device_t dev = sc->vmbus_dev;
	device_t bus = device_get_parent(device_get_parent(dev));
	int cpu;

	for (cpu = 0; cpu < ncpus; ++cpu) {
		struct vmbus_pcpu_data *psc = VMBUS_PCPU(sc, cpu);

		if (psc->intr_hand != NULL) {
			bus_teardown_intr(dev, psc->intr_res, psc->intr_hand);
			psc->intr_hand = NULL;
		}

		if (psc->intr_res != NULL) {
			BUS_RELEASE_RESOURCE(bus, dev, SYS_RES_IRQ,
			    psc->intr_rid, psc->intr_res);
			psc->intr_res = NULL;
		}

		if (psc->intr_rid != 0) {
			PCIB_RELEASE_MSIX(bus, dev, psc->intr_irq, psc->cpuid);
			psc->intr_rid = 0;
		}
	}
}

static void
vmbus_synic_setup(void *xsc)
{
	struct vmbus_softc *sc = xsc;
	struct vmbus_pcpu_data *psc = VMBUS_PCPU(sc, mycpuid);
	uint64_t val, orig;
	uint32_t sint;

	if (hyperv_features & CPUID_HV_MSR_VP_INDEX) {
		/*
		 * Save virtual processor id.
		 */
		psc->vcpuid = rdmsr(MSR_HV_VP_INDEX);
	} else {
		/*
		 * XXX
		 * Virtual processoor id is only used by a pretty broken
		 * channel selection code from storvsc.  It's nothing
		 * critical even if CPUID_HV_MSR_VP_INDEX is not set; keep
		 * moving on.
		 */
		psc->vcpuid = mycpuid;
	}

	/*
	 * Setup the SynIC message.
	 */
	orig = rdmsr(MSR_HV_SIMP);
	val = MSR_HV_SIMP_ENABLE | (orig & MSR_HV_SIMP_RSVD_MASK) |
	    ((psc->message_dma.hv_paddr >> PAGE_SHIFT) << MSR_HV_SIMP_PGSHIFT);
	wrmsr(MSR_HV_SIMP, val);

	/*
	 * Setup the SynIC event flags.
	 */
	orig = rdmsr(MSR_HV_SIEFP);
	val = MSR_HV_SIEFP_ENABLE | (orig & MSR_HV_SIEFP_RSVD_MASK) |
	    ((psc->event_flags_dma.hv_paddr >> PAGE_SHIFT) <<
	     MSR_HV_SIEFP_PGSHIFT);
	wrmsr(MSR_HV_SIEFP, val);


	/*
	 * Configure and unmask SINT for message and event flags.
	 */
	sint = MSR_HV_SINT0 + VMBUS_SINT_MESSAGE;
	orig = rdmsr(sint);
	val = psc->intr_vec | /* MSR_HV_SINT_AUTOEOI | notyet */
	    (orig & MSR_HV_SINT_RSVD_MASK);
	wrmsr(sint, val);

	/*
	 * Configure and unmask SINT for timer.
	 */
	sint = MSR_HV_SINT0 + VMBUS_SINT_TIMER;
	orig = rdmsr(sint);
	val = XTIMER_OFFSET | /* MSR_HV_SINT_AUTOEOI | notyet */
	    (orig & MSR_HV_SINT_RSVD_MASK);
	wrmsr(sint, val);

	/*
	 * All done; enable SynIC.
	 */
	orig = rdmsr(MSR_HV_SCONTROL);
	val = MSR_HV_SCTRL_ENABLE | (orig & MSR_HV_SCTRL_RSVD_MASK);
	wrmsr(MSR_HV_SCONTROL, val);
}

static void
vmbus_timer_stop(void *arg __unused)
{
	for (;;) {
		uint64_t val;

		/* Stop counting, and this also implies disabling STIMER0 */
		wrmsr(MSR_HV_STIMER0_COUNT, 0);

		val = rdmsr(MSR_HV_STIMER0_CONFIG);
		if ((val & MSR_HV_STIMER_CFG_ENABLE) == 0)
			break;
		cpu_pause();
	}
}

static void
vmbus_timer_config(void *arg __unused)
{
	/*
	 * Make sure that STIMER0 is really disabled before writing
	 * to STIMER0_CONFIG.
	 *
	 * "Writing to the configuration register of a timer that
	 *  is already enabled may result in undefined behaviour."
	 */
	vmbus_timer_stop(arg);
	wrmsr(MSR_HV_STIMER0_CONFIG,
	    MSR_HV_STIMER_CFG_AUTOEN | MSR_HV_STIMER0_CFG_SINT);
}

static void
vmbus_timer_msgintr(struct vmbus_pcpu_data *psc)
{
	volatile struct vmbus_message *msg;

	msg = psc->message + VMBUS_SINT_TIMER;
	if (msg->msg_type == HYPERV_MSGTYPE_TIMER_EXPIRED)
		vmbus_msg_reset(msg);
}

static void
vmbus_timer_restart(void *xsc)
{
	struct vmbus_softc *sc = xsc;
	struct vmbus_pcpu_data *psc = VMBUS_PCPU(sc, mycpuid);

	crit_enter();
	vmbus_timer_msgintr(psc);
	vmbus_timer_oneshot(psc, hyperv_tc64() + 1);
	crit_exit();
}

static void
vmbus_synic_teardown(void *arg __unused)
{
	uint64_t orig;
	uint32_t sint;

	/*
	 * Disable SynIC.
	 */
	orig = rdmsr(MSR_HV_SCONTROL);
	wrmsr(MSR_HV_SCONTROL, (orig & MSR_HV_SCTRL_RSVD_MASK));

	/*
	 * Mask message and event flags SINT.
	 */
	sint = MSR_HV_SINT0 + VMBUS_SINT_MESSAGE;
	orig = rdmsr(sint);
	wrmsr(sint, orig | MSR_HV_SINT_MASKED);

	/*
	 * Mask timer SINT.
	 */
	sint = MSR_HV_SINT0 + VMBUS_SINT_TIMER;
	orig = rdmsr(sint);
	wrmsr(sint, orig | MSR_HV_SINT_MASKED);

	/*
	 * Teardown SynIC message.
	 */
	orig = rdmsr(MSR_HV_SIMP);
	wrmsr(MSR_HV_SIMP, (orig & MSR_HV_SIMP_RSVD_MASK));

	/*
	 * Teardown SynIC event flags.
	 */
	orig = rdmsr(MSR_HV_SIEFP);
	wrmsr(MSR_HV_SIEFP, (orig & MSR_HV_SIEFP_RSVD_MASK));
}

static int
vmbus_init_contact(struct vmbus_softc *sc, uint32_t version)
{
	struct vmbus_chanmsg_init_contact *req;
	const struct vmbus_chanmsg_version_resp *resp;
	const struct vmbus_message *msg;
	struct vmbus_msghc *mh;
	int error, supp = 0;

	mh = vmbus_msghc_get(sc, sizeof(*req));
	if (mh == NULL)
		return ENXIO;

	req = vmbus_msghc_dataptr(mh);
	req->chm_hdr.chm_type = VMBUS_CHANMSG_TYPE_INIT_CONTACT;
	req->chm_ver = version;
	req->chm_evtflags = sc->vmbus_evtflags_dma.hv_paddr;
	req->chm_mnf1 = sc->vmbus_mnf1_dma.hv_paddr;
	req->chm_mnf2 = sc->vmbus_mnf2_dma.hv_paddr;

	error = vmbus_msghc_exec(sc, mh);
	if (error) {
		vmbus_msghc_put(sc, mh);
		return error;
	}

	msg = vmbus_msghc_wait_result(sc, mh);
	resp = (const struct vmbus_chanmsg_version_resp *)msg->msg_data;
	supp = resp->chm_supp;

	vmbus_msghc_put(sc, mh);

	return (supp ? 0 : EOPNOTSUPP);
}

static int
vmbus_init(struct vmbus_softc *sc)
{
	int i;

	for (i = 0; i < nitems(vmbus_version); ++i) {
		int error;

		error = vmbus_init_contact(sc, vmbus_version[i]);
		if (!error) {
			sc->vmbus_version = vmbus_version[i];
			device_printf(sc->vmbus_dev, "version %u.%u\n",
			    (sc->vmbus_version >> 16),
			    (sc->vmbus_version & 0xffff));
			return 0;
		}
	}
	return ENXIO;
}

static void
vmbus_chan_msgproc(struct vmbus_softc *sc, const struct vmbus_message *msg)
{
	const struct vmbus_chanmsg_hdr *hdr;

	hdr = (const struct vmbus_chanmsg_hdr *)msg->msg_data;

	/* TODO */
	if (hdr->chm_type == VMBUS_CHANMSG_TYPE_VERSION_RESP)
		vmbus_msghc_wakeup(sc, msg);
}
