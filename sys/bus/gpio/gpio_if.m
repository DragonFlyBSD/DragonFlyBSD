#-
# Copyright (c) 2016 The DragonFly Project.  All rights reserved.
#
# This code is derived from software contributed to The DragonFly Project
# by Imre Vadász <imre@vdsz.com>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of The DragonFly Project nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific, prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
# COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

#include <sys/bus.h>

INTERFACE gpio;

#
# Allocate GPIO interrupt.
# XXX trigger, polarity and termination constants are currently used from
#     sys/contrib/dev/acpica/source/include/acrestyp.h
#
METHOD int alloc_intr {
	device_t dev;
	u_int pin;
	int trigger;
	int polarity;
	int termination;
	void **cookiep;
};

#
# Deallocate GPIO interrupt.
#
METHOD void free_intr {
	device_t dev;
	void *cookie;
};

#
# Setup GPIO interrupt.
#
METHOD void setup_intr {
	device_t dev;
	void *cookie;
	void *arg;
	driver_intr_t *handler;
};

#
# Disable GPIO interrupt.
#
METHOD void teardown_intr {
	device_t dev;
	void *cookie;
};

#
# XXX Add a method for allocating pins for read/write IO.
#     Allocating a pin for IO should perform the necessary checks to
#     make sure that read_/write_pin doesn't trigger an assertion.
#

#
# Read pin value, returns 0 or 1.
#
METHOD int read_pin {
	device_t dev;
	u_int pin;
};

#
# Write pin value, value can be either 0 or 1.
#
METHOD void write_pin {
	device_t dev;
	u_int pin;
	int value;
};
