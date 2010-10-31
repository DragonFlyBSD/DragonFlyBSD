/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/i386/i386/elan-mmcr.c,v 1.6.2.1 2002/09/17 22:39:53 sam Exp $
 * The AMD Elan sc520 is a system-on-chip gadget which is used in embedded
 * kind of things, see www.soekris.com for instance, and it has a few quirks
 * we need to deal with.
 * Unfortunately we cannot identify the gadget by CPUID output because it
 * depends on strapping options and only the stepping field may be useful
 * and those are undocumented from AMDs side.
 *
 * So instead we recognize the on-chip host-PCI bridge and call back from
 * sys/i386/pci/pci_bus.c to here if we find it.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <machine/md_var.h>

#include <vm/vm.h>
#include <vm/pmap.h>

uint16_t *elan_mmcr;

#if 0

static unsigned
elan_get_timecount(struct timecounter *tc)
{
	return (elan_mmcr[0xc84 / 2]);
}

static struct timecounter elan_timecounter = {
	elan_get_timecount,
	0,
	0xffff,
	33333333 / 4,
	"ELAN"
};

#endif

void
init_AMD_Elan_sc520(void)
{
	u_int new;
	int i;

	if (bootverbose)
		kprintf("Doing h0h0magic for AMD Elan sc520\n");
	elan_mmcr = pmap_mapdev(0xfffef000, 0x1000);

	/*-
	 * The i8254 is driven with a nonstandard frequency which is
	 * derived thusly:
	 *   f = 32768 * 45 * 25 / 31 = 1189161.29...
	 * We use the sysctl to get the timecounter etc into whack.
	 */
	
	new = 1189161;
	i = kernel_sysctlbyname("machdep.i8254_freq", 
	    NULL, 0, 
	    &new, sizeof new, 
	    NULL);
	if (bootverbose)
		kprintf("sysctl machdep.i8254_freq=%d returns %d\n", new, i);

#if 0
	/* Start GP timer #2 and use it as timecounter, hz permitting */
	elan_mmcr[0xc82 / 2] = 0xc001;
	init_timecounter(&elan_timecounter);
#endif
}


/*
 * Device driver initialization stuff
 */

static d_open_t	elan_open;
static d_close_t elan_close;
static d_ioctl_t elan_ioctl;
static d_mmap_t elan_mmap;

#define CDEV_MAJOR 100
static struct dev_ops elan_ops = {
	{ "elan", 0, 0 },
	.d_open =	elan_open,
	.d_close =	elan_close,
	.d_ioctl =	elan_ioctl,
	.d_mmap =	elan_mmap,
};

static int
elan_open(struct dev_open_args *ap)
{
	return (0);
}

static int
elan_close(struct dev_close_args *ap)
{ 
	return (0);
}

static int
elan_mmap(struct dev_mmap_args *ap)
{
	if (ap->a_offset >= 0x1000) 
		return (EINVAL);
	ap->a_result = i386_btop(0xfffef000);
	return(0);
}

static int
elan_ioctl(struct dev_ioctl_args *ap)
{
	return(ENOENT);
}

static void
elan_drvinit(void)
{

	if (elan_mmcr == NULL)
		return;
	kprintf("Elan-mmcr driver: MMCR at %p\n", elan_mmcr);
	make_dev(&elan_ops, 0, UID_ROOT, GID_WHEEL, 0600, "elan-mmcr");
}

SYSINIT(elan, SI_SUB_PSEUDO, SI_ORDER_MIDDLE+CDEV_MAJOR,elan_drvinit,NULL);
