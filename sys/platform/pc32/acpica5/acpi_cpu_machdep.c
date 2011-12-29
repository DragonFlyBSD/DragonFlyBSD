#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

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
		if (cpu_feature2 & CPUID2_EST) {
			return (ACPI_PDC_PX_MSR |
			    ACPI_PDC_MP_PX_SWCORD |
			    ACPI_PDC_PX_HWCORD);
		}
	}
	return 0;
}
