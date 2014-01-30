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
	uint32_t features = 0;

	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		static int reported;

		if (!reported) {
			uint32_t regs[4];

			do_cpuid(0x6, regs);
			if (regs[0] & 0x2)
				kprintf("Turbo mode enabled in BIOS\n");
			reported = 1;
		}

		if (cpu_feature2 & CPUID2_EST) {
			features |= ACPI_PDC_PX_MSR |
			    ACPI_PDC_MP_PX_SWCOORD |
			    ACPI_PDC_PX_HWCOORD;
		}
		if ((cpu_feature2 & CPUID2_MON) &&
		    (cpu_mwait_feature &
		     (CPUID_MWAIT_EXT | CPUID_MWAIT_INTBRK)) ==
		    (CPUID_MWAIT_EXT | CPUID_MWAIT_INTBRK)) {
			features |= ACPI_PDC_MP_C1_NATIVE |
			    ACPI_PDC_MP_C2C3_NATIVE;
		}
	}
	return features;
}
