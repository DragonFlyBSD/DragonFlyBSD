#ifndef _ACPI_MD_CPU_H_
#define _ACPI_MD_CPU_H_

/*
 * CPU ID -> ACPI ID mapping macros
 */
#define CPUID_TO_ACPIID(cpu_id)		(cpu_id_to_acpi_id[(cpu_id)])

extern u_int			cpu_id_to_acpi_id[];

#endif	/* !_ACPI_MD_CPU_H_ */
