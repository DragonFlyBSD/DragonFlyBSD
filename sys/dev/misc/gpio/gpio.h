/*
 * Copyright (c) 2009 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * Copyright (c) 2008 Marc Balmer <mbalmer@openbsd.org>
 * Copyright (c) 2004, 2006 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/ioccom.h>

/* GPIO pin states */
#define GPIO_PIN_LOW		0x00	/* low level (logical 0) */
#define GPIO_PIN_HIGH		0x01	/* high level (logical 1) */

/* Max name length of a pin */
#define GPIOPINMAXNAME		64

/* GPIO pin configuration flags */
#define GPIO_PIN_INPUT		0x0001	/* input direction */
#define GPIO_PIN_OUTPUT		0x0002	/* output direction */
#define GPIO_PIN_INOUT		0x0004	/* bi-directional */
#define GPIO_PIN_OPENDRAIN	0x0008	/* open-drain output */
#define GPIO_PIN_PUSHPULL	0x0010	/* push-pull output */
#define GPIO_PIN_TRISTATE	0x0020	/* output disabled */
#define GPIO_PIN_PULLUP		0x0040	/* internal pull-up enabled */
#define GPIO_PIN_PULLDOWN	0x0080	/* internal pull-down enabled */
#define GPIO_PIN_INVIN		0x0100	/* invert input */
#define GPIO_PIN_INVOUT		0x0200	/* invert output */
#define GPIO_PIN_USER		0x0400	/* user != 0 can access */
#define GPIO_PIN_SET		0x8000	/* set for securelevel access */

/* Consumer attach arg types */
#define	GPIO_TYPE_INT		0x01

typedef struct gpio_pin {
	int	pin_num;		/* number */
	int	pin_caps;		/* capabilities */
	int	pin_flags;		/* current configuration */
	int	pin_state;		/* current state */
	int	pin_mapped;		/* is mapped */
	int	pin_opened;		/* is opened */
	cdev_t	dev;
} gpio_pin_t;

struct gpio {
	const char	*driver_name;
	void		*arg;
	int		(*pin_read)(void *, int);
	void		(*pin_write)(void *, int, int);
	void		(*pin_ctl)(void *, int, int);
	gpio_pin_t	*pins;
	int		npins;

	/* Private members */
	int		driver_unit;
	cdev_t		master_dev;
};

struct gpio_consumer {
	const char	*consumer_name;
	int		(*consumer_attach)(struct gpio *, void *, int, u_int32_t);
	int		(*consumer_detach)(struct gpio *, void *, int);
	LIST_ENTRY(gpio_consumer)	link;
};

struct gpio_info {
	int		npins;
	gpio_pin_t	*pins;
};

struct gpio_attach_args {
	char		consumer_name[16];
	int		pin_offset;
	u_int32_t	pin_mask;
	int		arg_type;
	union {
		char	string[32];
		long	lint;
	} consumer_arg;
};

struct gpio_pin_set_args {
	int	pin;
	int	caps;
	int	flags;
};

struct gpio_mapping {
	struct gpio	*gp;
	int		*map;
	int		size;

	int		map_alloced;
};

void	gpio_consumer_register(struct gpio_consumer *gcp);
void	gpio_consumer_unregister(struct gpio_consumer *gcp);
int	gpio_consumer_attach(const char *consumer, void *arg, struct gpio *gp,
	    int pin, u_int32_t mask);
int	gpio_consumer_detach(const char *consumer, struct gpio *gp, int pin);
struct gpio_mapping *gpio_map(struct gpio *gp, int *map, int offset, u_int32_t mask);
void	gpio_unmap(struct gpio_mapping *gmp);

int	gpio_npins(u_int32_t mask);
int	gpio_pin_read(struct gpio *gp, struct gpio_mapping *map, int pin);
void	gpio_pin_write(struct gpio  *gp, struct gpio_mapping *map, int pin, int data);
void	gpio_pin_ctl(struct gpio  *gp, struct gpio_mapping *map, int pin, int flags);
int	gpio_pin_caps(struct gpio  *gp, struct gpio_mapping *map, int pin);
void	gpio_register(struct gpio *gp);
void	gpio_unregister(struct gpio *gp);

void	led_switch(const char *name, int on_off);

#define GPIOINFO		_IOWR('G', 0, struct gpio_info)
#define GPIOPINSET		_IOWR('G', 4, struct gpio_pin_set_args)
#define GPIOPINUNSET		_IOWR('G', 5, struct gpio_pin_set_args)
#define GPIOATTACH		_IOWR('G', 6, struct gpio_attach_args)
#define GPIODETACH		_IOWR('G', 7, struct gpio_attach_args)
