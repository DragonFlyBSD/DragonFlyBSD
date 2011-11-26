#ifndef _ACPI_SDT_VAR_H_
#define _ACPI_SDT_VAR_H_

void		*sdt_sdth_map(vm_paddr_t);
void		sdt_sdth_unmap(struct acpi_sdth *);

vm_paddr_t	sdt_search(const uint8_t *);

#endif	/* !_ACPI_SDT_VAR_H_ */
