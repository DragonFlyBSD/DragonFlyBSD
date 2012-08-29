#ifndef _ACPI_SCI_VAR_H_
#define _ACPI_SCI_VAR_H_

#ifndef _SYS_BUS_H_
#include <sys/bus.h>
#endif

void	acpi_sci_config(void);
int	acpi_sci_enabled(void);
int	acpi_sci_pci_shariable(void);
int	acpi_sci_irqno(void);
void	acpi_sci_setmode1(enum intr_trigger, enum intr_polarity);

#endif	/* !_ACPI_SCI_VAR_H_ */
