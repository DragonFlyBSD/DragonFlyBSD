/*
 * Copyright (c) 2003-2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Mihai Carabas <mihai.carabas@gmail.com>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/assym.h>

#include "vmx.h"
#include "vmx_instr.h"

ASSYM(VTI_GUEST_RAX, offsetof(struct vmx_thread_info, guest.tf_rax));
ASSYM(VTI_GUEST_RBX, offsetof(struct vmx_thread_info, guest.tf_rbx));
ASSYM(VTI_GUEST_RCX, offsetof(struct vmx_thread_info, guest.tf_rcx));
ASSYM(VTI_GUEST_RDX, offsetof(struct vmx_thread_info, guest.tf_rdx));
ASSYM(VTI_GUEST_RSI, offsetof(struct vmx_thread_info, guest.tf_rsi));
ASSYM(VTI_GUEST_RDI, offsetof(struct vmx_thread_info, guest.tf_rdi));
ASSYM(VTI_GUEST_RBP, offsetof(struct vmx_thread_info, guest.tf_rbp));
ASSYM(VTI_GUEST_R8, offsetof(struct vmx_thread_info, guest.tf_r8));
ASSYM(VTI_GUEST_R9, offsetof(struct vmx_thread_info, guest.tf_r9));
ASSYM(VTI_GUEST_R10, offsetof(struct vmx_thread_info, guest.tf_r10));
ASSYM(VTI_GUEST_R11, offsetof(struct vmx_thread_info, guest.tf_r11));
ASSYM(VTI_GUEST_R12, offsetof(struct vmx_thread_info, guest.tf_r12));
ASSYM(VTI_GUEST_R13, offsetof(struct vmx_thread_info, guest.tf_r13));
ASSYM(VTI_GUEST_R14, offsetof(struct vmx_thread_info, guest.tf_r14));
ASSYM(VTI_GUEST_R15, offsetof(struct vmx_thread_info, guest.tf_r15));
ASSYM(VTI_GUEST_CR2, offsetof(struct vmx_thread_info, guest_cr2));

ASSYM(VTI_HOST_RBX, offsetof(struct vmx_thread_info, host_rbx));
ASSYM(VTI_HOST_RBP, offsetof(struct vmx_thread_info, host_rbp));
ASSYM(VTI_HOST_R10, offsetof(struct vmx_thread_info, host_r10));
ASSYM(VTI_HOST_R11, offsetof(struct vmx_thread_info, host_r11));
ASSYM(VTI_HOST_R12, offsetof(struct vmx_thread_info, host_r12));
ASSYM(VTI_HOST_R13, offsetof(struct vmx_thread_info, host_r13));
ASSYM(VTI_HOST_R14, offsetof(struct vmx_thread_info, host_r14));
ASSYM(VTI_HOST_R15, offsetof(struct vmx_thread_info, host_r15));
ASSYM(VTI_HOST_RSP, offsetof(struct vmx_thread_info, host_rsp));

ASSYM(VM_SUCCEED, VM_SUCCEED);
ASSYM(VM_FAIL_INVALID, VM_FAIL_INVALID);
ASSYM(VM_FAIL_VALID, VM_FAIL_VALID);
ASSYM(VM_EXIT, VM_EXIT);
