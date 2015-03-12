/*
 * Copyright 1996 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/i386/perfmon.c,v 1.21 1999/09/25 18:24:04 phk Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/fcntl.h>
#include <sys/lock.h>

#include <machine/clock.h>
#include <machine/perfmon.h>

static int perfmon_inuse;
static int perfmon_cpuok;
static int msr_pmc[NPMC];
static unsigned int ctl_shadow[NPMC];
static quad_t pmc_shadow[NPMC];	/* used when ctr is stopped on P5 */
static int (*writectl)(int);

static d_close_t	perfmon_close;
static d_open_t		perfmon_open;
static d_ioctl_t	perfmon_ioctl;

static struct dev_ops perfmon_ops = {
	{ "perfmon", 0, 0 },
	.d_open =	perfmon_open,
	.d_close =	perfmon_close,
	.d_ioctl =	perfmon_ioctl,
};

/*
 * Initialize the device ops for user access to the perfmon.  This must
 * be done late in the boot sequence.
 *
 * NOTE: The perfmon is really a minor of the mem major.  Perfmon
 * gets 32-47.
 */
static void
perfmon_driver_init(void *unused __unused)
{
	make_dev(&perfmon_ops, 32, UID_ROOT, GID_KMEM, 0640, "perfmon");
}

SYSINIT(perfmondrv, SI_SUB_DRIVERS, SI_ORDER_ANY, perfmon_driver_init, NULL);

/*
 * This is called in early boot, after cpu_class has been set up.
 */
void
perfmon_init(void)
{
}

int
perfmon_avail(void)
{
	return perfmon_cpuok;
}

int
perfmon_setup(int pmc, unsigned int control)
{
	if (pmc < 0 || pmc >= NPMC)
		return EINVAL;

	perfmon_inuse |= (1 << pmc);
	control &= ~(PMCF_SYS_FLAGS << 16);
	mpintr_lock();	/* doesn't have to be mpintr_lock YYY */
	ctl_shadow[pmc] = control;
	writectl(pmc);
	wrmsr(msr_pmc[pmc], pmc_shadow[pmc] = 0);
	mpintr_unlock();
	return 0;
}

int
perfmon_get(int pmc, unsigned int *control)
{
	if (pmc < 0 || pmc >= NPMC)
		return EINVAL;

	if (perfmon_inuse & (1 << pmc)) {
		*control = ctl_shadow[pmc];
		return 0;
	}
	return EBUSY;		/* XXX reversed sense */
}

int
perfmon_fini(int pmc)
{
	if (pmc < 0 || pmc >= NPMC)
		return EINVAL;

	if (perfmon_inuse & (1 << pmc)) {
		perfmon_stop(pmc);
		ctl_shadow[pmc] = 0;
		perfmon_inuse &= ~(1 << pmc);
		return 0;
	}
	return EBUSY;		/* XXX reversed sense */
}

int
perfmon_start(int pmc)
{
	if (pmc < 0 || pmc >= NPMC)
		return EINVAL;

	if (perfmon_inuse & (1 << pmc)) {
		mpintr_lock();	/* doesn't have to be mpintr YYY */
		ctl_shadow[pmc] |= (PMCF_EN << 16);
		wrmsr(msr_pmc[pmc], pmc_shadow[pmc]);
		writectl(pmc);
		mpintr_unlock();
		return 0;
	}
	return EBUSY;
}

int
perfmon_stop(int pmc)
{
	if (pmc < 0 || pmc >= NPMC)
		return EINVAL;

	if (perfmon_inuse & (1 << pmc)) {
		mpintr_lock();
		pmc_shadow[pmc] = rdmsr(msr_pmc[pmc]) & 0xffffffffffULL;
		ctl_shadow[pmc] &= ~(PMCF_EN << 16);
		writectl(pmc);
		mpintr_unlock();
		return 0;
	}
	return EBUSY;
}

int
perfmon_read(int pmc, quad_t *val)
{
	if (pmc < 0 || pmc >= NPMC)
		return EINVAL;

	if (perfmon_inuse & (1 << pmc)) {
		if (ctl_shadow[pmc] & (PMCF_EN << 16))
			*val = rdmsr(msr_pmc[pmc]) & 0xffffffffffULL;
		else
			*val = pmc_shadow[pmc];
		return 0;
	}

	return EBUSY;
}

int
perfmon_reset(int pmc)
{
	if (pmc < 0 || pmc >= NPMC)
		return EINVAL;

	if (perfmon_inuse & (1 << pmc)) {
		wrmsr(msr_pmc[pmc], pmc_shadow[pmc] = 0);
		return 0;
	}
	return EBUSY;
}

/*
 * Now the user-mode interface, called from a subdevice of mem.c.
 */
static int writer;
static int writerpmc;

static int
perfmon_open(struct dev_open_args *ap)
{
	if (!perfmon_cpuok)
		return ENXIO;

	if (ap->a_oflags & FWRITE) {
		if (writer) {
			return EBUSY;
		} else {
			writer = 1;
			writerpmc = 0;
		}
	}
	return 0;
}

static int
perfmon_close(struct dev_close_args *ap)
{
	if (ap->a_fflag & FWRITE) {
		int i;

		for (i = 0; i < NPMC; i++) {
			if (writerpmc & (1 << i))
				perfmon_fini(i);
		}
		writer = 0;
	}
	return 0;
}

static int
perfmon_ioctl(struct dev_ioctl_args *ap)
{
	caddr_t param = ap->a_data;
	struct pmc *pmc;
	struct pmc_data *pmcd;
	struct pmc_tstamp *pmct;
	int *ip;
	int rv;

	switch(ap->a_cmd) {
	case PMIOSETUP:
		if (!(ap->a_fflag & FWRITE))
			return EPERM;
		pmc = (struct pmc *)param;

		rv = perfmon_setup(pmc->pmc_num, pmc->pmc_val);
		if (!rv) {
			writerpmc |= (1 << pmc->pmc_num);
		}
		break;

	case PMIOGET:
		pmc = (struct pmc *)param;
		rv = perfmon_get(pmc->pmc_num, &pmc->pmc_val);
		break;

	case PMIOSTART:
		if (!(ap->a_fflag & FWRITE))
			return EPERM;

		ip = (int *)param;
		rv = perfmon_start(*ip);
		break;

	case PMIOSTOP:
		if (!(ap->a_fflag & FWRITE))
			return EPERM;

		ip = (int *)param;
		rv = perfmon_stop(*ip);
		break;

	case PMIORESET:
		if (!(ap->a_fflag & FWRITE))
			return EPERM;

		ip = (int *)param;
		rv = perfmon_reset(*ip);
		break;

	case PMIOREAD:
		pmcd = (struct pmc_data *)param;
		rv = perfmon_read(pmcd->pmcd_num, &pmcd->pmcd_value);
		break;

	case PMIOTSTAMP:
		if (tsc_frequency == 0) {
			rv = ENOTTY;
			break;
		}
		pmct = (struct pmc_tstamp *)param;
		/* XXX interface loses precision. */
		pmct->pmct_rate = (int)(tsc_frequency / 1000000);
		pmct->pmct_value = rdtsc();
		rv = 0;
		break;
	default:
		rv = ENOTTY;
	}

	return rv;
}
