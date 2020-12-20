/*
 * Copyright (c) 2020 François Tigeot <ftigeot@wolfpond.org>
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _DRM_LEASE_H
#define _DRM_LEASE_H

struct drm_file;
struct drm_master;

static inline bool
drm_lease_held(struct drm_file *file_priv, int id)
{
	return true;
}

static inline uint32_t
drm_lease_filter_crtcs(struct drm_file *file_priv, uint32_t crtcs)
{
	return crtcs;
}

static inline void
drm_lease_destroy(struct drm_master *lessee)
{
}

static inline void
drm_lease_revoke(struct drm_master *master)
{
}

#endif	/* _DRM_LEASE_H */
