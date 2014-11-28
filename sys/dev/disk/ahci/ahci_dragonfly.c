/*
 * (MPSAFE)
 *
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Primary device and CAM interface to OpenBSD AHCI driver, for DragonFly
 */

#include "ahci.h"

u_int32_t AhciForceGen = 0;
u_int32_t AhciNoFeatures = 0;

/*
 * Device bus methods
 */

static int	ahci_probe (device_t dev);
static int	ahci_attach (device_t dev);
static int	ahci_detach (device_t dev);
static int	ahci_sysctl_link_pwr_mgmt (SYSCTL_HANDLER_ARGS);
#if 0
static int	ahci_shutdown (device_t dev);
static int	ahci_suspend (device_t dev);
static int	ahci_resume (device_t dev);
#endif

static void	ahci_port_thread(void *arg);

static device_method_t ahci_methods[] = {
	DEVMETHOD(device_probe,		ahci_probe),
	DEVMETHOD(device_attach,	ahci_attach),
	DEVMETHOD(device_detach,	ahci_detach),
#if 0
	DEVMETHOD(device_shutdown,	ahci_shutdown),
	DEVMETHOD(device_suspend,	ahci_suspend),
	DEVMETHOD(device_resume,	ahci_resume),
#endif

	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),
	DEVMETHOD_END
};

static devclass_t	ahci_devclass;

static driver_t ahci_driver = {
	"ahci",
	ahci_methods,
	sizeof(struct ahci_softc)
};

MODULE_DEPEND(ahci, cam, 1, 1, 1);
DRIVER_MODULE(ahci, pci, ahci_driver, ahci_devclass, NULL, NULL);
MODULE_VERSION(ahci, 1);

/*
 * Device bus method procedures
 */
static int
ahci_probe (device_t dev)
{
	const struct ahci_device *ad;

	if (kgetenv("hint.ahci.disabled"))
		return(ENXIO);

	ad = ahci_lookup_device(dev);
	if (ad) {
		device_set_desc(dev, ad->name);
		return(-5);	/* higher priority the NATA */
	}
	return(ENXIO);
}

static int
ahci_attach (device_t dev)
{
	struct ahci_softc *sc = device_get_softc(dev);

	sc->sc_ad = ahci_lookup_device(dev);
	if (sc->sc_ad == NULL)
		return(ENXIO);

	/*
	 * Some chipsets do not properly implement the AHCI spec and may
	 * require the link speed to be specifically requested.
	 */
	if (kgetenv("hint.ahci.force150"))
		AhciForceGen = 1;
	if (kgetenv("hint.ahci.force300"))
		AhciForceGen = 2;
	if (kgetenv("hint.ahci.force600"))
		AhciForceGen = 3;

	if (kgetenv("hint.ahci.nofeatures"))
		AhciNoFeatures = -1;

	if (kgetenv("hint.ahci.forcefbss"))
		sc->sc_flags |= AHCI_F_FORCE_FBSS;

	return (sc->sc_ad->ad_attach(dev));
}

static int
ahci_detach (device_t dev)
{
	struct ahci_softc *sc = device_get_softc(dev);
	int error = 0;

	if (sc->sc_ad) {
		error = sc->sc_ad->ad_detach(dev);
		sc->sc_ad = NULL;
	}
	return(error);
}

static int
ahci_sysctl_link_pwr_mgmt (SYSCTL_HANDLER_ARGS)
{
	struct ahci_port *ap = arg1;
	int error, link_pwr_mgmt;

	link_pwr_mgmt = ap->link_pwr_mgmt;
	error = sysctl_handle_int(oidp, &link_pwr_mgmt, 0, req);
	if (error || req->newptr == NULL)
		return error;

	ahci_port_link_pwr_mgmt(ap, link_pwr_mgmt);
	return 0;
}

static int
ahci_sysctl_link_pwr_state (SYSCTL_HANDLER_ARGS)
{
	struct ahci_port *ap = arg1;
	const char *state_names[] = {"unknown", "active", "partial", "slumber"};
	char buf[16];
	int state;

	state = ahci_port_link_pwr_state(ap);
	if (state < 0 || state >= NELEM(state_names))
		state = 0;

	ksnprintf(buf, sizeof(buf), "%s", state_names[state]);
	return sysctl_handle_string(oidp, buf, sizeof(buf), req);
}

#if 0

static int
ahci_shutdown (device_t dev)
{
	return (0);
}

static int
ahci_suspend (device_t dev)
{
	return (0);
}

static int
ahci_resume (device_t dev)
{
	return (0);
}

#endif

/*
 * Sleep (ms) milliseconds, error on the side of caution.
 */
void
ahci_os_sleep(int ms)
{
	int ticks;

	ticks = hz * ms / 1000 + 1;
	tsleep(&ticks, 0, "ahslp", ticks);
}

/*
 * Sleep for a minimum interval and return the number of milliseconds
 * that was.  The minimum value returned is 1
 */
int
ahci_os_softsleep(void)
{
	if (hz >= 1000) {
		tsleep(&ticks, 0, "ahslp", hz / 1000);
		return(1);
	} else {
		tsleep(&ticks, 0, "ahslp", 1);
		return(1000 / hz);
	}
}

void
ahci_os_hardsleep(int us)
{
	DELAY(us);
}

/*
 * Create the OS-specific port helper thread and per-port lock.
 */
void
ahci_os_start_port(struct ahci_port *ap)
{
	struct sysctl_oid *soid;
	char name[16];

	atomic_set_int(&ap->ap_signal, AP_SIGF_INIT | AP_SIGF_THREAD_SYNC);
	lockinit(&ap->ap_lock, "ahcipo", 0, LK_CANRECURSE);
	lockinit(&ap->ap_sim_lock, "ahcicam", 0, LK_CANRECURSE);
	lockinit(&ap->ap_sig_lock, "ahport", 0, 0);
	sysctl_ctx_init(&ap->sysctl_ctx);
	ksnprintf(name, sizeof(name), "%d", ap->ap_num);
	soid = device_get_sysctl_tree(ap->ap_sc->sc_dev);
	ap->sysctl_tree = SYSCTL_ADD_NODE(&ap->sysctl_ctx,
				SYSCTL_CHILDREN(soid),
				OID_AUTO, name, CTLFLAG_RD, 0, "");

	if ((ap->ap_sc->sc_cap & AHCI_REG_CAP_SALP) &&
	    (ap->ap_sc->sc_cap & (AHCI_REG_CAP_PSC | AHCI_REG_CAP_SSC))) {
		SYSCTL_ADD_PROC(&ap->sysctl_ctx,
			SYSCTL_CHILDREN(ap->sysctl_tree), OID_AUTO,
			"link_pwr_mgmt", CTLTYPE_INT | CTLFLAG_RW, ap, 0,
			ahci_sysctl_link_pwr_mgmt, "I",
			"Link power management policy "
			"(0 = disabled, 1 = medium, 2 = aggressive)");
		SYSCTL_ADD_PROC(&ap->sysctl_ctx,
			SYSCTL_CHILDREN(ap->sysctl_tree), OID_AUTO,
			"link_pwr_state", CTLTYPE_STRING | CTLFLAG_RD, ap, 0,
			ahci_sysctl_link_pwr_state, "A",
			"Link power management state");

	}

	kthread_create(ahci_port_thread, ap, &ap->ap_thread,
		       "%s", PORTNAME(ap));
}

/*
 * Stop the OS-specific port helper thread and kill the per-port lock.
 */
void
ahci_os_stop_port(struct ahci_port *ap)
{
	if (ap->sysctl_tree) {
		sysctl_ctx_free(&ap->sysctl_ctx);
		ap->sysctl_tree = NULL;
	}

	if (ap->ap_thread) {
		ahci_os_signal_port_thread(ap, AP_SIGF_STOP);
		ahci_os_sleep(10);
		if (ap->ap_thread) {
			kprintf("%s: Waiting for thread to terminate\n",
				PORTNAME(ap));
			while (ap->ap_thread)
				ahci_os_sleep(100);
			kprintf("%s: thread terminated\n",
				PORTNAME(ap));
		}
	}
	lockuninit(&ap->ap_lock);
}

/*
 * Add (mask) to the set of bits being sent to the per-port thread helper
 * and wake the helper up if necessary.
 */
void
ahci_os_signal_port_thread(struct ahci_port *ap, int mask)
{
	lockmgr(&ap->ap_sig_lock, LK_EXCLUSIVE);
	atomic_set_int(&ap->ap_signal, mask);
	lockmgr(&ap->ap_sig_lock, LK_RELEASE);
	wakeup(&ap->ap_thread);
}

/*
 * Unconditionally lock the port structure for access.
 */
void
ahci_os_lock_port(struct ahci_port *ap)
{
	lockmgr(&ap->ap_lock, LK_EXCLUSIVE);
}

/*
 * Conditionally lock the port structure for access.
 *
 * Returns 0 on success, non-zero on failure.
 */
int
ahci_os_lock_port_nb(struct ahci_port *ap)
{
	return (lockmgr(&ap->ap_lock, LK_EXCLUSIVE | LK_NOWAIT));
}

/*
 * Unlock a previously locked port.
 */
void
ahci_os_unlock_port(struct ahci_port *ap)
{
	lockmgr(&ap->ap_lock, LK_RELEASE);
}

/*
 * Per-port thread helper.  This helper thread is responsible for
 * atomically retrieving and clearing the signal mask and calling
 * the machine-independant driver core.
 *
 * MPSAFE
 */
static
void
ahci_port_thread(void *arg)
{
	struct ahci_port *ap = arg;
	int mask;

	/*
	 * Sets us up as an interrupt support thread, meaning we are
	 * given a higher priority and we can preempt normal threads.
	 */
	lwkt_set_interrupt_support_thread();

	/*
	 * The helper thread is responsible for the initial port init,
	 * so all the ports can be inited in parallel.
	 *
	 * We also run the state machine which should do all probes.
	 * Since CAM is not attached yet we will not get out-of-order
	 * SCSI attachments.
	 */
	ahci_os_lock_port(ap);
	ahci_port_init(ap);
	atomic_clear_int(&ap->ap_signal, AP_SIGF_THREAD_SYNC);
	wakeup(&ap->ap_signal);
	ahci_port_state_machine(ap, 1);
	ahci_os_unlock_port(ap);
	atomic_clear_int(&ap->ap_signal, AP_SIGF_INIT);
	wakeup(&ap->ap_signal);

	/*
	 * Then loop on the helper core.
	 */
	mask = ap->ap_signal;
	while ((mask & AP_SIGF_STOP) == 0) {
		ahci_port_thread_core(ap, mask);
		lockmgr(&ap->ap_sig_lock, LK_EXCLUSIVE);
		if (ap->ap_signal == 0) {
			lksleep(&ap->ap_thread, &ap->ap_sig_lock, 0,
				"ahport", 0);
		}
		mask = ap->ap_signal;
		atomic_clear_int(&ap->ap_signal, mask);
		lockmgr(&ap->ap_sig_lock, LK_RELEASE);
	}
	ap->ap_thread = NULL;
}
