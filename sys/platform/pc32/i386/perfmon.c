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

#ifndef SMP
#include <machine/cputypes.h>
#endif
#include <machine/clock.h>
#include <machine/perfmon.h>

static int perfmon_inuse;
static int perfmon_cpuok;
#ifndef SMP
static int msr_ctl[NPMC];
#endif
static int msr_pmc[NPMC];
static unsigned int ctl_shadow[NPMC];
static quad_t pmc_shadow[NPMC];	/* used when ctr is stopped on P5 */
static int (*writectl)(int);
#ifndef SMP
static int writectl5(int);
static int writectl6(int);
#endif

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

SYSINIT(perfmondrv, SI_SUB_DRIVERS, SI_ORDER_ANY, perfmon_driver_init, NULL)

/*
 * This is called in early boot, after cpu_class has been set up.
 */
void
perfmon_init(void)
{
#ifndef SMP
	switch(cpu_class) {
	case CPUCLASS_586:
		perfmon_cpuok = 1;
		msr_ctl[0] = 0x11;
		msr_ctl[1] = 0x11;
		msr_pmc[0] = 0x12;
		msr_pmc[1] = 0x13;
		writectl = writectl5;
		break;
	case CPUCLASS_686:
		perfmon_cpuok = 1;
		msr_ctl[0] = 0x186;
		msr_ctl[1] = 0x187;
		msr_pmc[0] = 0xc1;
		msr_pmc[1] = 0xc2;
		writectl = writectl6;
		break;

	default:
		perfmon_cpuok = 0;
		break;
	}
#endif /* SMP */
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

#ifndef SMP
/*
 * Unfortunately, the performance-monitoring registers are laid out
 * differently in the P5 and P6.  We keep everything in P6 format
 * internally (except for the event code), and convert to P5
 * format as needed on those CPUs.  The writectl function pointer
 * is set up to point to one of these functions by perfmon_init().
 */
int
writectl6(int pmc)
{
	if (pmc > 0 && !(ctl_shadow[pmc] & (PMCF_EN << 16))) {
		wrmsr(msr_ctl[pmc], 0);
	} else {
		wrmsr(msr_ctl[pmc], ctl_shadow[pmc]);
	}
	return 0;
}

#define	P5FLAG_P	0x200
#define	P5FLAG_E	0x100
#define	P5FLAG_USR	0x80
#define	P5FLAG_OS	0x40

int
writectl5(int pmc)
{
	quad_t newval = 0;

	if (ctl_shadow[1] & (PMCF_EN << 16)) {
		if (ctl_shadow[1] & (PMCF_USR << 16))
			newval |= P5FLAG_USR << 16;
		if (ctl_shadow[1] & (PMCF_OS << 16))
			newval |= P5FLAG_OS << 16;
		if (!(ctl_shadow[1] & (PMCF_E << 16)))
			newval |= P5FLAG_E << 16;
		newval |= (ctl_shadow[1] & 0x3f) << 16;
	}
	if (ctl_shadow[0] & (PMCF_EN << 16)) {
		if (ctl_shadow[0] & (PMCF_USR << 16))
			newval |= P5FLAG_USR;
		if (ctl_shadow[0] & (PMCF_OS << 16))
			newval |= P5FLAG_OS;
		if (!(ctl_shadow[0] & (PMCF_E << 16)))
			newval |= P5FLAG_E;
		newval |= ctl_shadow[0] & 0x3f;
	}

	wrmsr(msr_ctl[0], newval);
	return 0;		/* XXX should check for unimplemented bits */
}
#endif /* !SMP */

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
