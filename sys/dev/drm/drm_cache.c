/**************************************************************************
 *
 * Copyright (c) 2006-2007 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include <linux/export.h>
#include <drm/drmP.h>

#include <machine/md_var.h>
#include <machine/cpufunc.h>

#define cpu_has_clflush	1

void
drm_clflush_virt_range(char *addr, unsigned long length)
{
	if (cpu_has_clflush) {
		char *end = addr + length;
		cpu_mfence();
		for (; addr < end; addr += cpu_clflush_line_size)
			clflush((unsigned long)addr);
		clflush((unsigned long)(end - 1));
		cpu_mfence();
		return;
	}

	cpu_wbinvd_on_all_cpus();
}
EXPORT_SYMBOL(drm_clflush_virt_range);
