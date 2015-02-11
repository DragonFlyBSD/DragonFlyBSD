#include <sys/bus.h>

INTERFACE coremctl;

METHOD int mch_read {
	device_t		dev;
	int			reg;
	uint32_t		*val;
};
