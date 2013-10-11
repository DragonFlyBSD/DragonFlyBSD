#
# Copyright (c) 1998 Doug Rabson
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
# $FreeBSD: src/sys/kern/bus_if.m,v 1.16 1999/10/12 21:35:50 dfr Exp $
#

#include <sys/bus.h>

INTERFACE bus;

#
# Default implementations of some methods.
#
CODE {
	static struct resource *
	null_alloc_resource(device_t dev, device_t child,
			    int type, int *rid,
			    u_long start, u_long end,
			    u_long count, u_int flags, int cpuid)
	{
	    return 0;
	}
};

#
# This is called from system code which prints out a description of a
# device.  It should describe the attachment that the child has with
# the parent.  See bus_generic_print_child.9 for more information.
# This method returns the number of characters output.
#
METHOD int print_child {
	device_t dev;
	device_t child;
} DEFAULT bus_generic_print_child;

# 
# Called for each child device that 
# did not succeed in probing for a
# driver.
#    
METHOD void probe_nomatch {
        device_t dev;
        device_t child;
};

#
# These two methods manage a bus specific set of instance variables of
# a child device.  The intention is that each different type of bus
# defines a set of appropriate instance variables (such as ports and
# irqs for ISA bus etc.)
#
# This information could be given to the child device as a struct but
# that makes it hard for a bus to add or remove variables without
# forcing an edit and recompile for all drivers which may not be
# possible for vendor supplied binary drivers.

#
# Read an instance variable.  Return 0 on success.
#
METHOD int read_ivar {
	device_t dev;
	device_t child;
	int index;
	uintptr_t *result;
};

#
# Write an instance variable.  Return 0 on success.
#
METHOD int write_ivar {
	device_t dev;
	device_t child;
	int index;
	uintptr_t value;
};

#
# Called after the child's DEVICE_DETACH method to allow the parent
# to reclaim any resources allocated on behalf of the child.
#
METHOD void child_detached {
	device_t dev;
	device_t child;
};

#
# Called when a new driver is added to the devclass which owns this
# bus. The generic implementation of this method attempts to probe and
# attach any un-matched children of the bus.
#
METHOD void driver_added {
	device_t dev;
	driver_t *driver;
} DEFAULT bus_generic_driver_added;

#
# For busses which use drivers supporting DEVICE_IDENTIFY to
# enumerate their devices, these methods are used to create new
# device instances. If place is non-NULL, the new device will be
# added after the last existing child with the same order.
#
# bus is an entity which may iterate up through the bus heirarchy
# while parent is the parent device under which the child should be
# added.
#
METHOD device_t add_child {
	device_t bus;
	device_t parent;
	int order;
	const char *name;
	int unit;
};

#
# Allocate a system resource attached to `dev' on behalf of `child'.
# The types are defined in <sys/bus_resource.h>; the meaning of the
# resource-ID field varies from bus to bus (but *rid == 0 is always
# valid if the resource type is).  start and end reflect the allowable
# range, and should be passed as `0UL' and `~0UL', respectively, if
# the client has no range restriction.  count is the number of consecutive
# indices in the resource required.  flags is a set of sharing flags
# as defined in <sys/rman.h>.
#
# Returns a resource or a null pointer on failure.  The caller is
# responsible for calling rman_activate_resource() when it actually
# uses the resource.
#
METHOD struct resource * alloc_resource {
	device_t	dev;
	device_t	child;
	int		type;
	int	       *rid;
	u_long		start;
	u_long		end;
	u_long		count;
	u_int		flags;
	int		cpuid;
} DEFAULT null_alloc_resource;

METHOD int activate_resource {
	device_t	dev;
	device_t	child;
	int		type;
	int		rid;
	struct resource *r;
};

METHOD int deactivate_resource {
	device_t	dev;
	device_t	child;
	int		type;
	int		rid;
	struct resource *r;
};

#
# Free a resource allocated by the preceding method.  The `rid' value
# must be the same as the one returned by BUS_ALLOC_RESOURCE (which
# is not necessarily the same as the one the client passed).
#
METHOD int release_resource {
	device_t	dev;
	device_t	child;
	int		type;
	int		rid;
	struct resource *res;
};

METHOD int setup_intr {
	device_t	dev;
	device_t	child;
	struct resource *irq;
	int		flags;
	driver_intr_t	*intr;
	void		*arg;
	void		**cookiep;
	lwkt_serialize_t serializer;
	const char	*desc;
};

METHOD int teardown_intr {
	device_t	dev;
	device_t	child;
	struct resource	*irq;
	void		*cookie;
};

# Enable or disable an interrupt.  The device is generally expected to do
# the physical enablement and disablement.  The bus code must flag the
# condition so it does not call the handler from a scheduled interrupt thread,
# since the hard interrupt might be disabled after the interrupt thread
# has been scheduled but before it runs.
#
# The disable function returns an indication as to whether the handler
# is currently running (i.e. the disablement is racing the execution of
# the interrupt handler).  0 is returned if it isn't, non-zero if it is.
#
# The disablement function does NOT interlock against a running handler, it
# simply prevents future handler calls from being made.
#
METHOD void enable_intr {
	device_t	dev;
	device_t	child;
	void		*cookie;
} DEFAULT bus_generic_enable_intr;

METHOD int disable_intr {
	device_t	dev;
	device_t	child;
	void		*cookie;
} DEFAULT bus_generic_disable_intr;

#
# Set the range used for a particular resource. Return EINVAL if
# the type or rid are out of range.
#
METHOD int set_resource {
	device_t	dev;
	device_t	child;
	int		type;
	int		rid;
	u_long		start;
	u_long		count;
	int		cpuid;
};

#
# Get the range for a resource. Return ENOENT if the type or rid are
# out of range or have not been set.
#
METHOD int get_resource {
	device_t	dev;
	device_t	child;
	int		type;
	int		rid;
	u_long		*startp;
	u_long		*countp;
};

#
# Delete a resource.
#
METHOD void delete_resource {
	device_t	dev;
	device_t	child;
	int		type;
	int		rid;
};

#
# Return a struct resource_list.
#
METHOD struct resource_list * get_resource_list {
	device_t	_dev;
	device_t	_child;
} DEFAULT bus_generic_get_resource_list;

#
# Is the hardware described by _child still attached to the system?
#
# This method should return 0 if the device is not present.  It should
# return -1 if it is present.  Any errors in determining should be
# returned as a normal errno value.  Client drivers are to assume that
# the device is present, even if there is an error determining if it is
# there.  Busses are to try to avoid returning errors, but newcard will return
# an error if the device fails to implement this method.
#
METHOD int child_present {
	device_t	_dev;
	device_t	_child;
} DEFAULT bus_generic_child_present;

#
# Returns the pnp info for this device.  Return it as a string.  If the
# string is insufficient for the storage, then return EOVERFLOW.
#
METHOD int child_pnpinfo_str {
	device_t	_dev;
	device_t	_child;
	char		*_buf;
	size_t		_buflen;
};

#
# Returns the location for this device.  Return it as a string.  If the
# string is insufficient for the storage, then return EOVERFLOW.
#
METHOD int child_location_str {
	device_t	_dev;
	device_t	_child;
	char		*_buf;
	size_t		_buflen;
};

#
# Allow (bus) drivers to specify the trigger mode and polarity of the
# specified interrupt.
#
METHOD int config_intr {
        device_t        _dev;
	device_t	_child;
        int             _irq;
        enum intr_trigger _trig;
        enum intr_polarity _pol;
} DEFAULT bus_generic_config_intr;

/**
 * @brief Returns bus_dma_tag_t for use w/ devices on the bus.
 *
 * @param _dev		the parent device of @p _child
 * @param _child	the device to which the tag will belong
 */
METHOD bus_dma_tag_t get_dma_tag {
	device_t	_dev;
	device_t	_child;
} DEFAULT bus_generic_get_dma_tag;
