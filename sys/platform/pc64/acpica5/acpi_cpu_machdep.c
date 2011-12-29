#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#include "acpi.h"
#include "acpivar.h"
#include "acpi_cpu.h"

uint32_t
acpi_cpu_md_features(void)
{
	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		uint32_t regs[4];
		static int reported;

		if (!reported) {
			do_cpuid(0x6, regs);
			if (regs[0] & 0x2)
				kprintf("Turbo mode enabled in BIOS\n");
			reported = 1;
		}

		if (cpu_feature2 & CPUID2_EST) {
			return (ACPI_PDC_PX_MSR |
			    ACPI_PDC_MP_PX_SWCOORD |
			    ACPI_PDC_PX_HWCOORD);
		}
	}
	return 0;
}
