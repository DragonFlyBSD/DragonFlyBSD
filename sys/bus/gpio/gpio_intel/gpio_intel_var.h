#ifndef _GPIO_INTEL_VAR_H
#define _GPIO_INTEL_VAR_H

struct pinrange {
	int start;
	int end;
};

struct pin_intr_map {
	int pin;
	int intidx;
	void *arg;
	driver_intr_t *handler;
	int is_level;
	uint32_t orig_intcfg;
	uint32_t orig_gpiocfg;
};

struct pin_io_map {
	int pin;
	int flags;
};

struct gpio_intel_softc {
	device_t dev;
	struct resource *mem_res;
	struct resource *irq_res;
	void		*intrhand;
	struct lock	lk;
	struct pinrange *ranges;
	struct pin_intr_map intrmaps[16];
	struct pin_io_map iomaps[128];
	struct gpio_intel_fns *fns;
};

typedef	void(*gpio_intel_init_fn)(struct gpio_intel_softc *sc);
typedef	int(*gpio_intel_map_intr_fn)(struct gpio_intel_softc *sc,
	    uint16_t pin, int trigger, int polarity, int termination);
typedef	void(*gpio_intel_unmap_intr_fn)(struct gpio_intel_softc *sc,
	    struct pin_intr_map *map);
typedef	void(*gpio_intel_enable_intr_fn)(struct gpio_intel_softc *sc,
	    struct pin_intr_map *map);
typedef	void(*gpio_intel_disable_intr_fn)(struct gpio_intel_softc *sc,
	    struct pin_intr_map *map);
typedef int(*gpio_intel_check_io_pin_fn)(struct gpio_intel_softc *sc,
	    uint16_t pin, int flags);
typedef	void(*gpio_intel_write_pin_fn)(struct gpio_intel_softc *sc,
	    uint16_t pin, int value);
typedef	int(*gpio_intel_read_pin_fn)(struct gpio_intel_softc *sc,
	    uint16_t pin);

struct gpio_intel_fns {
	gpio_intel_init_fn	init;
	driver_intr_t		*intr;
	gpio_intel_map_intr_fn	map_intr;
	gpio_intel_unmap_intr_fn unmap_intr;
	gpio_intel_enable_intr_fn enable_intr;
	gpio_intel_disable_intr_fn disable_intr;
	gpio_intel_check_io_pin_fn check_io_pin;
	gpio_intel_write_pin_fn	write_pin;
	gpio_intel_read_pin_fn	read_pin;
};

int	gpio_cherryview_matchuid(struct gpio_intel_softc *sc);

#endif
