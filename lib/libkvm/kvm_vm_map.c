/*
 * Copyright (c) 2019 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@dragonflybsd.org>
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
 */

/*
 * Useful helper functions for vm_map_t parsing.
 */

#include <sys/user.h>
#include <sys/param.h>
#include <vm/vm.h>
#include <vm/vm_map.h>

#include "kvm.h"

static int
kreadent(kvm_t *kd, const void *kaddr, vm_map_entry_t copy)
{
	size_t nb;

	nb = sizeof(*copy);

	if (kvm_read(kd, (u_long)kaddr, (char *)copy, nb) == (ssize_t)nb)
		return 1;

	return 0;
}

/*
 * Find and read first vm_map entry.
 */
vm_map_entry_t
kvm_vm_map_entry_first(kvm_t *kd, vm_map_t map, vm_map_entry_t copy)
{
	vm_map_entry_t ken;

	ken = map->rb_root.rbh_root;
	if (ken == NULL)
		return NULL;
	if (!kreadent(kd, ken, copy))
		return NULL;
	while (copy->rb_entry.rbe_left) {
		ken = copy->rb_entry.rbe_left;
		if (!kreadent(kd, ken, copy))
			return NULL;
	}
	return ken;
}

/*
 * Find and read next vm_map entry.
 */
vm_map_entry_t
kvm_vm_map_entry_next(kvm_t *kd, vm_map_entry_t ken, vm_map_entry_t copy)
{
	vm_map_entry_t ken2;

	if (copy->rb_entry.rbe_right) {
		ken = copy->rb_entry.rbe_right;
		if (!kreadent(kd, ken, copy))
			return NULL;
		while (copy->rb_entry.rbe_left) {
			ken = copy->rb_entry.rbe_left;
			if (!kreadent(kd, ken, copy))
				return NULL;
		}
	} else {
		if ((ken2 = copy->rb_entry.rbe_parent) == NULL)
			return NULL;
		if (!kreadent(kd, ken2, copy))
			return NULL;
		if (ken == copy->rb_entry.rbe_left) {
			ken = ken2;
		} else {
			while (ken == copy->rb_entry.rbe_right) {
				ken = ken2;
				ken2 = copy->rb_entry.rbe_parent;
				if (!kreadent(kd, ken2, copy))
					return NULL;
			}
			ken = ken2;
		}
	}
	return ken;
}
