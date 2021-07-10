/*
 * Copyright (c) 2018-2021 Maxime Villard, m00nbsd.net
 * All rights reserved.
 *
 * This code is part of the NVMM hypervisor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/mman.h>

#include "nvmm.h"
#include "nvmm_internal.h"
#include "nvmm_ioctl.h"

static struct nvmm_machine machines[NVMM_MAX_MACHINES];
static volatile unsigned int nmachines __cacheline_aligned;

static const struct nvmm_impl *nvmm_impl_list[] = {
#if defined(__x86_64__)
	&nvmm_x86_svm,	/* x86 AMD SVM */
	&nvmm_x86_vmx	/* x86 Intel VMX */
#endif
};

static const struct nvmm_impl *nvmm_impl __read_mostly = NULL;

static struct nvmm_owner root_owner;

/* -------------------------------------------------------------------------- */

static int
nvmm_machine_alloc(struct nvmm_machine **ret)
{
	struct nvmm_machine *mach;
	size_t i;

	for (i = 0; i < NVMM_MAX_MACHINES; i++) {
		mach = &machines[i];

		os_rwl_wlock(&mach->lock);
		if (mach->present) {
			os_rwl_unlock(&mach->lock);
			continue;
		}

		mach->present = true;
		mach->time = time_second;
		*ret = mach;
		os_atomic_inc_uint(&nmachines);
		return 0;
	}

	return ENOBUFS;
}

static void
nvmm_machine_free(struct nvmm_machine *mach)
{
	OS_ASSERT(os_rwl_wheld(&mach->lock));
	OS_ASSERT(mach->present);
	mach->present = false;
	os_atomic_dec_uint(&nmachines);
}

static int
nvmm_machine_get(struct nvmm_owner *owner, nvmm_machid_t machid,
    struct nvmm_machine **ret, bool writer)
{
	struct nvmm_machine *mach;

	if (__predict_false(machid >= NVMM_MAX_MACHINES)) {
		return EINVAL;
	}
	mach = &machines[machid];

	if (__predict_false(writer)) {
		os_rwl_wlock(&mach->lock);
	} else {
		os_rwl_rlock(&mach->lock);
	}
	if (__predict_false(!mach->present)) {
		os_rwl_unlock(&mach->lock);
		return ENOENT;
	}
	if (__predict_false(mach->owner != owner && owner != &root_owner)) {
		os_rwl_unlock(&mach->lock);
		return EPERM;
	}
	*ret = mach;

	return 0;
}

static void
nvmm_machine_put(struct nvmm_machine *mach)
{
	os_rwl_unlock(&mach->lock);
}

/* -------------------------------------------------------------------------- */

static int
nvmm_vcpu_alloc(struct nvmm_machine *mach, nvmm_cpuid_t cpuid,
    struct nvmm_cpu **ret)
{
	struct nvmm_cpu *vcpu;

	if (cpuid >= NVMM_MAX_VCPUS) {
		return EINVAL;
	}
	vcpu = &mach->cpus[cpuid];

	os_mtx_lock(&vcpu->lock);
	if (vcpu->present) {
		os_mtx_unlock(&vcpu->lock);
		return EBUSY;
	}

	vcpu->present = true;
	vcpu->comm = NULL;
	vcpu->hcpu_last = -1;
	*ret = vcpu;
	return 0;
}

static void
nvmm_vcpu_free(struct nvmm_machine *mach, struct nvmm_cpu *vcpu)
{
	OS_ASSERT(os_mtx_owned(&vcpu->lock));
	vcpu->present = false;
	if (vcpu->comm != NULL) {
		os_vmobj_unmap(os_kernel_map, (vaddr_t)vcpu->comm,
		    (vaddr_t)vcpu->comm + PAGE_SIZE, true);
	}
}

static int
nvmm_vcpu_get(struct nvmm_machine *mach, nvmm_cpuid_t cpuid,
    struct nvmm_cpu **ret)
{
	struct nvmm_cpu *vcpu;

	if (__predict_false(cpuid >= NVMM_MAX_VCPUS)) {
		return EINVAL;
	}
	vcpu = &mach->cpus[cpuid];

	os_mtx_lock(&vcpu->lock);
	if (__predict_false(!vcpu->present)) {
		os_mtx_unlock(&vcpu->lock);
		return ENOENT;
	}
	*ret = vcpu;

	return 0;
}

static void
nvmm_vcpu_put(struct nvmm_cpu *vcpu)
{
	os_mtx_unlock(&vcpu->lock);
}

/* -------------------------------------------------------------------------- */

static void
nvmm_kill_machines(struct nvmm_owner *owner)
{
	struct nvmm_machine *mach;
	struct nvmm_cpu *vcpu;
	size_t i, j;
	int error;

	for (i = 0; i < NVMM_MAX_MACHINES; i++) {
		mach = &machines[i];

		os_rwl_wlock(&mach->lock);
		if (!mach->present || mach->owner != owner) {
			os_rwl_unlock(&mach->lock);
			continue;
		}

		/* Kill it. */
		for (j = 0; j < NVMM_MAX_VCPUS; j++) {
			error = nvmm_vcpu_get(mach, j, &vcpu);
			if (error)
				continue;
			(*nvmm_impl->vcpu_destroy)(mach, vcpu);
			nvmm_vcpu_free(mach, vcpu);
			nvmm_vcpu_put(vcpu);
			os_atomic_dec_uint(&mach->ncpus);
		}
		(*nvmm_impl->machine_destroy)(mach);
		os_vmspace_destroy(mach->vm);

		/* Drop the kernel vmobj refs. */
		for (j = 0; j < NVMM_MAX_HMAPPINGS; j++) {
			if (!mach->hmap[j].present)
				continue;
			os_vmobj_rel(mach->hmap[j].vmobj);
		}

		nvmm_machine_free(mach);

		os_rwl_unlock(&mach->lock);
	}
}

/* -------------------------------------------------------------------------- */

static int
nvmm_capability(struct nvmm_owner *owner, struct nvmm_ioc_capability *args)
{
	args->cap.version = NVMM_KERN_VERSION;
	args->cap.state_size = nvmm_impl->state_size;
	args->cap.max_machines = NVMM_MAX_MACHINES;
	args->cap.max_vcpus = NVMM_MAX_VCPUS;
	args->cap.max_ram = NVMM_MAX_RAM;

	(*nvmm_impl->capability)(&args->cap);

	return 0;
}

static int
nvmm_machine_create(struct nvmm_owner *owner,
    struct nvmm_ioc_machine_create *args)
{
	struct nvmm_machine *mach;
	int error;

	error = nvmm_machine_alloc(&mach);
	if (error)
		return error;

	/* Curproc owns the machine. */
	mach->owner = owner;

	/* Zero out the host mappings. */
	memset(&mach->hmap, 0, sizeof(mach->hmap));

	/* Create the machine vmspace. */
	mach->gpa_begin = 0;
	mach->gpa_end = NVMM_MAX_RAM;
	mach->vm = os_vmspace_create(mach->gpa_begin, mach->gpa_end);

#ifdef __DragonFly__
	/*
	 * Set PMAP_MULTI on the backing pmap for the machine.  Only
	 * pmap changes to the backing pmap for the machine affect the
	 * guest.  Changes to the host's pmap do not affect the guest's
	 * backing pmap.
	 */
	pmap_maybethreaded(&mach->vm->vm_pmap);
#endif

	/* Create the comm vmobj. */
	mach->commvmobj = os_vmobj_create(NVMM_MAX_VCPUS * PAGE_SIZE);

	(*nvmm_impl->machine_create)(mach);

	args->machid = mach->machid;
	nvmm_machine_put(mach);

	return 0;
}

static int
nvmm_machine_destroy(struct nvmm_owner *owner,
    struct nvmm_ioc_machine_destroy *args)
{
	struct nvmm_machine *mach;
	struct nvmm_cpu *vcpu;
	int error;
	size_t i;

	error = nvmm_machine_get(owner, args->machid, &mach, true);
	if (error)
		return error;

	for (i = 0; i < NVMM_MAX_VCPUS; i++) {
		error = nvmm_vcpu_get(mach, i, &vcpu);
		if (error)
			continue;

		(*nvmm_impl->vcpu_destroy)(mach, vcpu);
		nvmm_vcpu_free(mach, vcpu);
		nvmm_vcpu_put(vcpu);
		os_atomic_dec_uint(&mach->ncpus);
	}

	(*nvmm_impl->machine_destroy)(mach);

	/* Free the machine vmspace. */
	os_vmspace_destroy(mach->vm);

	/* Drop the kernel vmobj refs. */
	for (i = 0; i < NVMM_MAX_HMAPPINGS; i++) {
		if (!mach->hmap[i].present)
			continue;
		os_vmobj_rel(mach->hmap[i].vmobj);
	}

	nvmm_machine_free(mach);
	nvmm_machine_put(mach);

	return 0;
}

static int
nvmm_machine_configure(struct nvmm_owner *owner,
    struct nvmm_ioc_machine_configure *args)
{
	struct nvmm_machine *mach;
	size_t allocsz;
	uint64_t op;
	void *data;
	int error;

	op = NVMM_MACH_CONF_MD(args->op);
	if (__predict_false(op >= nvmm_impl->mach_conf_max)) {
		return EINVAL;
	}

	allocsz = nvmm_impl->mach_conf_sizes[op];
	data = os_mem_alloc(allocsz);

	error = nvmm_machine_get(owner, args->machid, &mach, true);
	if (error) {
		os_mem_free(data, allocsz);
		return error;
	}

	error = copyin(args->conf, data, allocsz);
	if (error) {
		goto out;
	}

	error = (*nvmm_impl->machine_configure)(mach, op, data);

out:
	nvmm_machine_put(mach);
	os_mem_free(data, allocsz);
	return error;
}

static int
nvmm_vcpu_create(struct nvmm_owner *owner, struct nvmm_ioc_vcpu_create *args)
{
	struct nvmm_machine *mach;
	struct nvmm_cpu *vcpu;
	int error;

	error = nvmm_machine_get(owner, args->machid, &mach, false);
	if (error)
		return error;

	error = nvmm_vcpu_alloc(mach, args->cpuid, &vcpu);
	if (error)
		goto out;

	/* Map the comm page on the kernel side, as wired. */
	error = os_vmobj_map(os_kernel_map, (vaddr_t *)&vcpu->comm, PAGE_SIZE,
	    mach->commvmobj, args->cpuid * PAGE_SIZE, true /* wired */,
	    false /* !fixed */, true /* shared */, PROT_READ | PROT_WRITE,
	    PROT_READ | PROT_WRITE);
	if (error) {
		nvmm_vcpu_free(mach, vcpu);
		nvmm_vcpu_put(vcpu);
		goto out;
	}

	memset(vcpu->comm, 0, PAGE_SIZE);

	/* Map the comm page on the user side, as pageable. */
	error = os_vmobj_map(os_curproc_map, (vaddr_t *)&args->comm, PAGE_SIZE,
	    mach->commvmobj, args->cpuid * PAGE_SIZE, false /* !wired */,
	    false /* !fixed */, true /* shared */, PROT_READ | PROT_WRITE,
	    PROT_READ | PROT_WRITE);
	if (error) {
		nvmm_vcpu_free(mach, vcpu);
		nvmm_vcpu_put(vcpu);
		goto out;
	}

	error = (*nvmm_impl->vcpu_create)(mach, vcpu);
	if (error) {
		nvmm_vcpu_free(mach, vcpu);
		nvmm_vcpu_put(vcpu);
		goto out;
	}

	nvmm_vcpu_put(vcpu);
	os_atomic_inc_uint(&mach->ncpus);

out:
	nvmm_machine_put(mach);
	return error;
}

static int
nvmm_vcpu_destroy(struct nvmm_owner *owner, struct nvmm_ioc_vcpu_destroy *args)
{
	struct nvmm_machine *mach;
	struct nvmm_cpu *vcpu;
	int error;

	error = nvmm_machine_get(owner, args->machid, &mach, false);
	if (error)
		return error;

	error = nvmm_vcpu_get(mach, args->cpuid, &vcpu);
	if (error)
		goto out;

	(*nvmm_impl->vcpu_destroy)(mach, vcpu);
	nvmm_vcpu_free(mach, vcpu);
	nvmm_vcpu_put(vcpu);
	os_atomic_dec_uint(&mach->ncpus);

out:
	nvmm_machine_put(mach);
	return error;
}

static int
nvmm_vcpu_configure(struct nvmm_owner *owner,
    struct nvmm_ioc_vcpu_configure *args)
{
	struct nvmm_machine *mach;
	struct nvmm_cpu *vcpu;
	size_t allocsz;
	uint64_t op;
	void *data;
	int error;

	op = NVMM_VCPU_CONF_MD(args->op);
	if (__predict_false(op >= nvmm_impl->vcpu_conf_max))
		return EINVAL;

	allocsz = nvmm_impl->vcpu_conf_sizes[op];
	data = os_mem_alloc(allocsz);

	error = nvmm_machine_get(owner, args->machid, &mach, false);
	if (error) {
		os_mem_free(data, allocsz);
		return error;
	}

	error = nvmm_vcpu_get(mach, args->cpuid, &vcpu);
	if (error) {
		nvmm_machine_put(mach);
		os_mem_free(data, allocsz);
		return error;
	}

	error = copyin(args->conf, data, allocsz);
	if (error) {
		goto out;
	}

	error = (*nvmm_impl->vcpu_configure)(vcpu, op, data);

out:
	nvmm_vcpu_put(vcpu);
	nvmm_machine_put(mach);
	os_mem_free(data, allocsz);
	return error;
}

static int
nvmm_vcpu_setstate(struct nvmm_owner *owner,
    struct nvmm_ioc_vcpu_setstate *args)
{
	struct nvmm_machine *mach;
	struct nvmm_cpu *vcpu;
	int error;

	error = nvmm_machine_get(owner, args->machid, &mach, false);
	if (error)
		return error;

	error = nvmm_vcpu_get(mach, args->cpuid, &vcpu);
	if (error)
		goto out;

	(*nvmm_impl->vcpu_setstate)(vcpu);
	nvmm_vcpu_put(vcpu);

out:
	nvmm_machine_put(mach);
	return error;
}

static int
nvmm_vcpu_getstate(struct nvmm_owner *owner,
    struct nvmm_ioc_vcpu_getstate *args)
{
	struct nvmm_machine *mach;
	struct nvmm_cpu *vcpu;
	int error;

	error = nvmm_machine_get(owner, args->machid, &mach, false);
	if (error)
		return error;

	error = nvmm_vcpu_get(mach, args->cpuid, &vcpu);
	if (error)
		goto out;

	(*nvmm_impl->vcpu_getstate)(vcpu);
	nvmm_vcpu_put(vcpu);

out:
	nvmm_machine_put(mach);
	return error;
}

static int
nvmm_vcpu_inject(struct nvmm_owner *owner, struct nvmm_ioc_vcpu_inject *args)
{
	struct nvmm_machine *mach;
	struct nvmm_cpu *vcpu;
	int error;

	error = nvmm_machine_get(owner, args->machid, &mach, false);
	if (error)
		return error;

	error = nvmm_vcpu_get(mach, args->cpuid, &vcpu);
	if (error)
		goto out;

	error = (*nvmm_impl->vcpu_inject)(vcpu);
	nvmm_vcpu_put(vcpu);

out:
	nvmm_machine_put(mach);
	return error;
}

static int
nvmm_do_vcpu_run(struct nvmm_machine *mach, struct nvmm_cpu *vcpu,
    struct nvmm_vcpu_exit *exit)
{
	struct vmspace *vm = mach->vm;
	int ret;

	while (1) {
		/* Got a signal? Or pending resched? Leave. */
		if (__predict_false(os_return_needed())) {
			exit->reason = NVMM_VCPU_EXIT_NONE;
			return 0;
		}

		/* Run the VCPU. */
		ret = (*nvmm_impl->vcpu_run)(mach, vcpu, exit);
		if (__predict_false(ret != 0)) {
			return ret;
		}

		/* Process nested page faults. */
		if (__predict_true(exit->reason != NVMM_VCPU_EXIT_MEMORY)) {
			break;
		}
		if (exit->u.mem.gpa >= mach->gpa_end) {
			break;
		}
		if (os_vmspace_fault(vm, exit->u.mem.gpa, exit->u.mem.prot)) {
			break;
		}
	}

	return 0;
}

static int
nvmm_vcpu_run(struct nvmm_owner *owner, struct nvmm_ioc_vcpu_run *args)
{
	struct nvmm_machine *mach;
	struct nvmm_cpu *vcpu;
	int error;

	error = nvmm_machine_get(owner, args->machid, &mach, false);
	if (error)
		return error;

	error = nvmm_vcpu_get(mach, args->cpuid, &vcpu);
	if (error)
		goto out;

	error = nvmm_do_vcpu_run(mach, vcpu, &args->exit);
	nvmm_vcpu_put(vcpu);

out:
	nvmm_machine_put(mach);
	return error;
}

/* -------------------------------------------------------------------------- */

static os_vmobj_t *
nvmm_hmapping_getvmobj(struct nvmm_machine *mach, uintptr_t hva, size_t size,
   size_t *off)
{
	struct nvmm_hmapping *hmapping;
	size_t i;

	for (i = 0; i < NVMM_MAX_HMAPPINGS; i++) {
		hmapping = &mach->hmap[i];
		if (!hmapping->present) {
			continue;
		}
		if (hva >= hmapping->hva &&
		    hva + size <= hmapping->hva + hmapping->size) {
			*off = hva - hmapping->hva;
			return hmapping->vmobj;
		}
	}

	return NULL;
}

static int
nvmm_hmapping_validate(struct nvmm_machine *mach, uintptr_t hva, size_t size)
{
	struct nvmm_hmapping *hmapping;
	size_t i;

	if ((hva % PAGE_SIZE) != 0 || (size % PAGE_SIZE) != 0) {
		return EINVAL;
	}
	if (hva == 0) {
		return EINVAL;
	}

	for (i = 0; i < NVMM_MAX_HMAPPINGS; i++) {
		hmapping = &mach->hmap[i];
		if (!hmapping->present) {
			continue;
		}

		if (hva >= hmapping->hva &&
		    hva + size <= hmapping->hva + hmapping->size) {
			break;
		}

		if (hva >= hmapping->hva &&
		    hva < hmapping->hva + hmapping->size) {
			return EEXIST;
		}
		if (hva + size > hmapping->hva &&
		    hva + size <= hmapping->hva + hmapping->size) {
			return EEXIST;
		}
		if (hva <= hmapping->hva &&
		    hva + size >= hmapping->hva + hmapping->size) {
			return EEXIST;
		}
	}

	return 0;
}

static struct nvmm_hmapping *
nvmm_hmapping_alloc(struct nvmm_machine *mach)
{
	struct nvmm_hmapping *hmapping;
	size_t i;

	for (i = 0; i < NVMM_MAX_HMAPPINGS; i++) {
		hmapping = &mach->hmap[i];
		if (!hmapping->present) {
			hmapping->present = true;
			return hmapping;
		}
	}

	return NULL;
}

static int
nvmm_hmapping_free(struct nvmm_machine *mach, uintptr_t hva, size_t size)
{
	struct nvmm_hmapping *hmapping;
	size_t i;

	for (i = 0; i < NVMM_MAX_HMAPPINGS; i++) {
		hmapping = &mach->hmap[i];
		if (!hmapping->present || hmapping->hva != hva ||
		    hmapping->size != size) {
			continue;
		}

		os_vmobj_unmap(os_curproc_map, hmapping->hva,
		    hmapping->hva + hmapping->size, false);
		os_vmobj_rel(hmapping->vmobj);

		hmapping->vmobj = NULL;
		hmapping->present = false;

		return 0;
	}

	return ENOENT;
}

static int
nvmm_hva_map(struct nvmm_owner *owner, struct nvmm_ioc_hva_map *args)
{
	struct nvmm_machine *mach;
	struct nvmm_hmapping *hmapping;
	vaddr_t uva;
	int error;

	error = nvmm_machine_get(owner, args->machid, &mach, true);
	if (error)
		return error;

	error = nvmm_hmapping_validate(mach, args->hva, args->size);
	if (error)
		goto out;

	hmapping = nvmm_hmapping_alloc(mach);
	if (hmapping == NULL) {
		error = ENOBUFS;
		goto out;
	}

	hmapping->hva = args->hva;
	hmapping->size = args->size;
	hmapping->vmobj = os_vmobj_create(hmapping->size);
	uva = hmapping->hva;

	/* Map the vmobj into the user address space, as pageable. */
	error = os_vmobj_map(os_curproc_map, &uva, hmapping->size,
	    hmapping->vmobj, 0, false /* !wired */, true /* fixed */,
	    true /* shared */, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);

out:
	nvmm_machine_put(mach);
	return error;
}

static int
nvmm_hva_unmap(struct nvmm_owner *owner, struct nvmm_ioc_hva_unmap *args)
{
	struct nvmm_machine *mach;
	int error;

	error = nvmm_machine_get(owner, args->machid, &mach, true);
	if (error)
		return error;

	error = nvmm_hmapping_free(mach, args->hva, args->size);

	nvmm_machine_put(mach);
	return error;
}

/* -------------------------------------------------------------------------- */

static int
nvmm_gpa_map(struct nvmm_owner *owner, struct nvmm_ioc_gpa_map *args)
{
	struct nvmm_machine *mach;
	os_vmobj_t *vmobj;
	gpaddr_t gpa;
	size_t off;
	int error;

	error = nvmm_machine_get(owner, args->machid, &mach, false);
	if (error)
		return error;

	if ((args->prot & ~(PROT_READ|PROT_WRITE|PROT_EXEC)) != 0) {
		error = EINVAL;
		goto out;
	}

	if ((args->gpa % PAGE_SIZE) != 0 || (args->size % PAGE_SIZE) != 0 ||
	    (args->hva % PAGE_SIZE) != 0) {
		error = EINVAL;
		goto out;
	}
	if (args->hva == 0) {
		error = EINVAL;
		goto out;
	}
	if (args->gpa < mach->gpa_begin || args->gpa >= mach->gpa_end) {
		error = EINVAL;
		goto out;
	}
	if (args->gpa + args->size <= args->gpa) {
		error = EINVAL;
		goto out;
	}
	if (args->gpa + args->size > mach->gpa_end) {
		error = EINVAL;
		goto out;
	}
	gpa = args->gpa;

	vmobj = nvmm_hmapping_getvmobj(mach, args->hva, args->size, &off);
	if (vmobj == NULL) {
		error = EINVAL;
		goto out;
	}

	/* Map the vmobj into the machine address space, as pageable. */
	error = os_vmobj_map(&mach->vm->vm_map, &gpa, args->size, vmobj, off,
	    false /* !wired */, true /* fixed */, false /* !shared */,
	    args->prot, PROT_READ | PROT_WRITE | PROT_EXEC);

out:
	nvmm_machine_put(mach);
	return error;
}

static int
nvmm_gpa_unmap(struct nvmm_owner *owner, struct nvmm_ioc_gpa_unmap *args)
{
	struct nvmm_machine *mach;
	gpaddr_t gpa;
	int error;

	error = nvmm_machine_get(owner, args->machid, &mach, false);
	if (error)
		return error;

	if ((args->gpa % PAGE_SIZE) != 0 || (args->size % PAGE_SIZE) != 0) {
		error = EINVAL;
		goto out;
	}
	if (args->gpa < mach->gpa_begin || args->gpa >= mach->gpa_end) {
		error = EINVAL;
		goto out;
	}
	if (args->gpa + args->size <= args->gpa) {
		error = EINVAL;
		goto out;
	}
	if (args->gpa + args->size >= mach->gpa_end) {
		error = EINVAL;
		goto out;
	}
	gpa = args->gpa;

	/* Unmap the memory from the machine. */
	os_vmobj_unmap(&mach->vm->vm_map, gpa, gpa + args->size, false);

out:
	nvmm_machine_put(mach);
	return error;
}

/* -------------------------------------------------------------------------- */

static int
nvmm_ctl_mach_info(struct nvmm_owner *owner, struct nvmm_ioc_ctl *args)
{
	struct nvmm_ctl_mach_info ctl;
	struct nvmm_machine *mach;
	int error;
	size_t i;

	if (args->size != sizeof(ctl))
		return EINVAL;
	error = copyin(args->data, &ctl, sizeof(ctl));
	if (error)
		return error;

	error = nvmm_machine_get(owner, ctl.machid, &mach, true);
	if (error)
		return error;

	ctl.nvcpus = mach->ncpus;

	ctl.nram = 0;
	for (i = 0; i < NVMM_MAX_HMAPPINGS; i++) {
		if (!mach->hmap[i].present)
			continue;
		ctl.nram += mach->hmap[i].size;
	}

	ctl.pid = mach->owner->pid;
	ctl.time = mach->time;

	nvmm_machine_put(mach);

	error = copyout(&ctl, args->data, sizeof(ctl));
	if (error)
		return error;

	return 0;
}

static int
nvmm_ctl(struct nvmm_owner *owner, struct nvmm_ioc_ctl *args)
{
	switch (args->op) {
	case NVMM_CTL_MACH_INFO:
		return nvmm_ctl_mach_info(owner, args);
	default:
		return EINVAL;
	}
}

/* -------------------------------------------------------------------------- */

static const struct nvmm_impl *
nvmm_ident(void)
{
	size_t i;

	for (i = 0; i < __arraycount(nvmm_impl_list); i++) {
		if ((*nvmm_impl_list[i]->ident)())
			return nvmm_impl_list[i];
	}

	return NULL;
}

static int
nvmm_init(void)
{
	size_t i, n;

	nvmm_impl = nvmm_ident();
	if (nvmm_impl == NULL)
		return ENOTSUP;

	for (i = 0; i < NVMM_MAX_MACHINES; i++) {
		machines[i].machid = i;
		os_rwl_init(&machines[i].lock);
		for (n = 0; n < NVMM_MAX_VCPUS; n++) {
			machines[i].cpus[n].present = false;
			machines[i].cpus[n].cpuid = n;
			os_mtx_init(&machines[i].cpus[n].lock);
		}
	}

	(*nvmm_impl->init)();

	return 0;
}

static void
nvmm_fini(void)
{
	size_t i, n;

	for (i = 0; i < NVMM_MAX_MACHINES; i++) {
		os_rwl_destroy(&machines[i].lock);
		for (n = 0; n < NVMM_MAX_VCPUS; n++) {
			os_mtx_destroy(&machines[i].cpus[n].lock);
		}
	}

	(*nvmm_impl->fini)();
	nvmm_impl = NULL;
}

/* -------------------------------------------------------------------------- */

static int
nvmm_ioctl(struct nvmm_owner *owner, unsigned long cmd, void *data)
{
	switch (cmd) {
	case NVMM_IOC_CAPABILITY:
		return nvmm_capability(owner, data);
	case NVMM_IOC_MACHINE_CREATE:
		return nvmm_machine_create(owner, data);
	case NVMM_IOC_MACHINE_DESTROY:
		return nvmm_machine_destroy(owner, data);
	case NVMM_IOC_MACHINE_CONFIGURE:
		return nvmm_machine_configure(owner, data);
	case NVMM_IOC_VCPU_CREATE:
		return nvmm_vcpu_create(owner, data);
	case NVMM_IOC_VCPU_DESTROY:
		return nvmm_vcpu_destroy(owner, data);
	case NVMM_IOC_VCPU_CONFIGURE:
		return nvmm_vcpu_configure(owner, data);
	case NVMM_IOC_VCPU_SETSTATE:
		return nvmm_vcpu_setstate(owner, data);
	case NVMM_IOC_VCPU_GETSTATE:
		return nvmm_vcpu_getstate(owner, data);
	case NVMM_IOC_VCPU_INJECT:
		return nvmm_vcpu_inject(owner, data);
	case NVMM_IOC_VCPU_RUN:
		return nvmm_vcpu_run(owner, data);
	case NVMM_IOC_GPA_MAP:
		return nvmm_gpa_map(owner, data);
	case NVMM_IOC_GPA_UNMAP:
		return nvmm_gpa_unmap(owner, data);
	case NVMM_IOC_HVA_MAP:
		return nvmm_hva_map(owner, data);
	case NVMM_IOC_HVA_UNMAP:
		return nvmm_hva_unmap(owner, data);
	case NVMM_IOC_CTL:
		return nvmm_ctl(owner, data);
	default:
		return EINVAL;
	}
}

/* -------------------------------------------------------------------------- */

#if defined(__DragonFly__)

#include <sys/conf.h>
#include <sys/devfs.h>
#include <sys/device.h>
#include <sys/module.h>

static d_open_t dfbsd_nvmm_open;
static d_ioctl_t dfbsd_nvmm_ioctl;
static d_priv_dtor_t dfbsd_nvmm_dtor;

static struct dev_ops nvmm_ops = {
	{ "nvmm", 0, D_MPSAFE },
	.d_open		= dfbsd_nvmm_open,
	.d_ioctl	= dfbsd_nvmm_ioctl,
};

static int
dfbsd_nvmm_open(struct dev_open_args *ap)
{
	int flags = ap->a_oflags;
	struct nvmm_owner *owner;
	struct file *fp;
	int error;

	if (__predict_false(nvmm_impl == NULL))
		return ENXIO;
	if (!(flags & O_CLOEXEC))
		return EINVAL;

	if (OFLAGS(flags) & O_WRONLY) {
		owner = &root_owner;
	} else {
		owner = os_mem_alloc(sizeof(*owner));
		owner->pid = curthread->td_proc->p_pid;
	}

	fp = ap->a_fpp ? *ap->a_fpp : NULL;
	error = devfs_set_cdevpriv(fp, owner, dfbsd_nvmm_dtor);
	if (error) {
		dfbsd_nvmm_dtor(owner);
		return error;
	}

	return 0;
}

static void
dfbsd_nvmm_dtor(void *arg)
{
	struct nvmm_owner *owner = arg;

	OS_ASSERT(owner != NULL);
	nvmm_kill_machines(owner);
	if (owner != &root_owner) {
		os_mem_free(owner, sizeof(*owner));
	}
}

static int
dfbsd_nvmm_ioctl(struct dev_ioctl_args *ap)
{
	unsigned long cmd = ap->a_cmd;
	void *data = ap->a_data;
	struct file *fp = ap->a_fp;
	struct nvmm_owner *owner = NULL;

	devfs_get_cdevpriv(fp, (void **)&owner);
	OS_ASSERT(owner != NULL);

	return nvmm_ioctl(owner, cmd, data);
}

/* -------------------------------------------------------------------------- */

static int
nvmm_attach(void)
{
	int error;

	error = nvmm_init();
	if (error)
		panic("%s: impossible", __func__);
	os_printf("nvmm: attached, using backend %s\n", nvmm_impl->name);

	return 0;
}

static int
nvmm_detach(void)
{
	if (os_atomic_load_uint(&nmachines) > 0)
		return EBUSY;

	nvmm_fini();
	return 0;
}

static int
nvmm_modevent(module_t mod __unused, int type, void *data __unused)
{
	static cdev_t dev = NULL;
	int error;

	switch (type) {
	case MOD_LOAD:
		if (nvmm_ident() == NULL) {
			os_printf("nvmm: cpu not supported\n");
			return ENOTSUP;
		}
		error = nvmm_attach();
		if (error)
			return error;

		dev = make_dev(&nvmm_ops, 0, UID_ROOT, GID_NVMM, 0640, "nvmm");
		if (dev == NULL) {
			os_printf("nvmm: unable to create device\n");
			error = ENOMEM;
		}
		break;

	case MOD_UNLOAD:
		if (dev == NULL)
			return 0;
		error = nvmm_detach();
		if (error == 0)
			destroy_dev(dev);
		break;

	case MOD_SHUTDOWN:
		error = 0;
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return error;
}

static moduledata_t nvmm_moddata = {
	.name = "nvmm",
	.evhand = nvmm_modevent,
	.priv = NULL,
};

DECLARE_MODULE(nvmm, nvmm_moddata, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(nvmm, NVMM_KERN_VERSION);

#endif /* __DragonFly__ */
