#include <sys/bus.h>
#include <sys/sensors.h>

INTERFACE cpu;

METHOD struct ksensordev * get_sensdev {
	device_t	dev;
};
