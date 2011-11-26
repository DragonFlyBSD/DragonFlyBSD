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
 * Primary device and CAM interface to OpenBSD SILI driver, for DragonFly
 */

#include "sili.h"

u_int32_t SiliForceGen1 = 0;
u_int32_t SiliNoFeatures = 0;

/*
 * Device bus methods
 */

static int	sili_probe (device_t dev);
static int	sili_attach (device_t dev);
static int	sili_detach (device_t dev);
#if 0
static int	sili_shutdown (device_t dev);
static int	sili_suspend (device_t dev);
static int	sili_resume (device_t dev);
#endif

static void	sili_port_thread(void *arg);

static device_method_t sili_methods[] = {
	DEVMETHOD(device_probe,		sili_probe),
	DEVMETHOD(device_attach,	sili_attach),
	DEVMETHOD(device_detach,	sili_detach),
#if 0
	DEVMETHOD(device_shutdown,	sili_shutdown),
	DEVMETHOD(device_suspend,	sili_suspend),
	DEVMETHOD(device_resume,	sili_resume),
#endif

	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),
	{0, 0}
};

static devclass_t	sili_devclass;

static driver_t sili_driver = {
	"sili",
	sili_methods,
	sizeof(struct sili_softc)
};

MODULE_DEPEND(sili, cam, 1, 1, 1);
DRIVER_MODULE(sili, pci, sili_driver, sili_devclass, NULL, NULL);

/*
 * Device bus method procedures
 */
static int
sili_probe (device_t dev)
{
	const struct sili_device *ad;

	if (kgetenv("hint.sili.disabled"))
		return(ENXIO);
	if (kgetenv("hint.sili.force150"))
		SiliForceGen1 = -1;
	if (kgetenv("hint.sili.nofeatures"))
		SiliNoFeatures = -1;

	ad = sili_lookup_device(dev);
	if (ad) {
		device_set_desc(dev, ad->name);
		return(-5);	/* higher priority the NATA */
	}
	return(ENXIO);
}

static int
sili_attach (device_t dev)
{
	struct sili_softc *sc = device_get_softc(dev);
	int error;

	sc->sc_ad = sili_lookup_device(dev);
	if (sc->sc_ad == NULL)
		return(ENXIO);
	error = sc->sc_ad->ad_attach(dev);
	return (error);
}

static int
sili_detach (device_t dev)
{
	struct sili_softc *sc = device_get_softc(dev);
	int error = 0;

	if (sc->sc_ad) {
		error = sc->sc_ad->ad_detach(dev);
		sc->sc_ad = NULL;
	}
	return(error);
}

#if 0

static int
sili_shutdown (device_t dev)
{
	return (0);
}

static int
sili_suspend (device_t dev)
{
	return (0);
}

static int
sili_resume (device_t dev)
{
	return (0);
}

#endif

/*
 * Sleep (ms) milliseconds, error on the side of caution.
 */
void
sili_os_sleep(int ms)
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
sili_os_softsleep(void)
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
sili_os_hardsleep(int us)
{
	DELAY(us);
}

/*
 * Create the OS-specific port helper thread and per-port lock.
 */
void
sili_os_start_port(struct sili_port *ap)
{
	atomic_set_int(&ap->ap_signal, AP_SIGF_INIT);
	lockinit(&ap->ap_lock, "silipo", 0, 0);
	lockinit(&ap->ap_sim_lock, "silicam", 0, LK_CANRECURSE);
	lockinit(&ap->ap_sig_lock, "siport", 0, 0);
	kthread_create(sili_port_thread, ap, &ap->ap_thread,
		       "%s", PORTNAME(ap));
}

/*
 * Stop the OS-specific port helper thread and kill the per-port lock.
 */
void
sili_os_stop_port(struct sili_port *ap)
{
	if (ap->ap_thread) {
		sili_os_signal_port_thread(ap, AP_SIGF_STOP);
		sili_os_sleep(10);
		if (ap->ap_thread) {
			kprintf("%s: Waiting for thread to terminate\n",
				PORTNAME(ap));
			while (ap->ap_thread)
				sili_os_sleep(100);
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
sili_os_signal_port_thread(struct sili_port *ap, int mask)
{
	lockmgr(&ap->ap_sig_lock, LK_EXCLUSIVE);
	atomic_set_int(&ap->ap_signal, mask);
	wakeup(&ap->ap_thread);
	lockmgr(&ap->ap_sig_lock, LK_RELEASE);
}

/*
 * Unconditionally lock the port structure for access.
 */
void
sili_os_lock_port(struct sili_port *ap)
{
	lockmgr(&ap->ap_lock, LK_EXCLUSIVE);
}

/*
 * Conditionally lock the port structure for access.
 *
 * Returns 0 on success, non-zero on failure.
 */
int
sili_os_lock_port_nb(struct sili_port *ap)
{
	return (lockmgr(&ap->ap_lock, LK_EXCLUSIVE | LK_NOWAIT));
}

/*
 * Unlock a previously locked port.
 */
void
sili_os_unlock_port(struct sili_port *ap)
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
sili_port_thread(void *arg)
{
	struct sili_port *ap = arg;
	int mask;

	/*
	 * The helper thread is responsible for the initial port init,
	 * so all the ports can be inited in parallel.
	 *
	 * We also run the state machine which should do all probes.
	 * Since CAM is not attached yet we will not get out-of-order
	 * SCSI attachments.
	 */
	sili_os_lock_port(ap);
	sili_port_init(ap);
	sili_port_state_machine(ap, 1);
	sili_os_unlock_port(ap);
	atomic_clear_int(&ap->ap_signal, AP_SIGF_INIT);
	wakeup(&ap->ap_signal);

	/*
	 * Then loop on the helper core.
	 */
	mask = ap->ap_signal;
	while ((mask & AP_SIGF_STOP) == 0) {
		atomic_clear_int(&ap->ap_signal, mask);
		sili_port_thread_core(ap, mask);
		lockmgr(&ap->ap_sig_lock, LK_EXCLUSIVE);
		if (ap->ap_signal == 0) {
			lksleep(&ap->ap_thread, &ap->ap_sig_lock, 0,
				"siport", 0);
		}
		lockmgr(&ap->ap_sig_lock, LK_RELEASE);
		mask = ap->ap_signal;
	}
	ap->ap_thread = NULL;
}
