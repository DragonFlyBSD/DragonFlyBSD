#-
# Copyright (c) 2011, Bryan Venteicher <bryanv@daemoninthecloset.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD: src/sys/dev/virtio/virtio_bus_if.m,v 1.1 2011/11/18 05:43:43 grehan Exp $

#include <sys/bus.h>

INTERFACE virtio_bus;

HEADER {
struct vq_alloc_info;
};

METHOD uint64_t negotiate_features {
	device_t	dev;
	uint64_t	child_features;
};

METHOD int bind_intr {
	device_t	dev;
	uint		irq;
	int		what;
	driver_intr_t	handler;
	void		*arg;
};

METHOD int unbind_intr {
	device_t	dev;
	int		what;
};

METHOD int with_feature {
	device_t	dev;
	uint64_t	feature;
};

METHOD int intr_count {
	device_t	dev;
};

METHOD int intr_alloc {
	device_t	dev;
	int		*cnt;
	int		use_config;
	int		*cpus;
};

METHOD int intr_release {
	device_t	dev;
};

METHOD int alloc_virtqueues {
	device_t	dev;
	int		nvqs;
	struct vq_alloc_info *info;
};

METHOD int setup_intr {
	device_t		dev;
	uint			irq;
	lwkt_serialize_t	slz;
};

METHOD int teardown_intr {
	device_t		dev;
	uint			irq;
};

METHOD void stop {
	device_t		dev;
};

METHOD int reinit {
	device_t		dev;
	uint64_t		features;
};

METHOD void reinit_complete {
	device_t		dev;
};

METHOD void notify_vq {
	device_t		dev;
	uint16_t		queue;
};

METHOD void read_device_config {
	device_t		dev;
	bus_size_t		offset;
	void			*dst;
	int			len;
};

METHOD void write_device_config {
	device_t		dev;
	bus_size_t		offset;
	void			*src;
	int			len;
};
