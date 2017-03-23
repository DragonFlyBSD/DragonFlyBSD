#ifndef _DRM_AGPSUPPORT_H_
#define _DRM_AGPSUPPORT_H_

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/kconfig.h>
#include <sys/agpio.h>
#include <dev/agp/agpvar.h>
#include <uapi_drm/drm.h>

struct drm_device;
struct drm_file;

struct drm_agp_head {
	device_t	agpdev;
	struct agp_info	agp_info;
	struct drm_agp_mem *memory;
	unsigned long mode;
	int enabled;
	int acquired;
	unsigned long base;
	int agp_mtrr;
	int cant_use_aperture;
	unsigned long page_mask;
};

#define CONFIG_AGP 1
#if IS_ENABLED(CONFIG_AGP)

#ifdef __linux__
void drm_free_agp(struct agp_memory * handle, int pages);
int drm_bind_agp(struct agp_memory * handle, unsigned int start);
int drm_unbind_agp(struct agp_memory * handle);
struct agp_memory *drm_agp_bind_pages(struct drm_device *dev,
				struct page **pages,
				unsigned long num_pages,
				uint32_t gtt_offset,
				uint32_t type);
#endif

struct drm_agp_head *drm_agp_init(struct drm_device *dev);
void drm_legacy_agp_clear(struct drm_device *dev);
int drm_agp_acquire(struct drm_device *dev);
int drm_agp_acquire_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
int drm_agp_release(struct drm_device *dev);
int drm_agp_release_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
int drm_agp_enable(struct drm_device *dev, struct drm_agp_mode mode);
int drm_agp_enable_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int drm_agp_info(struct drm_device *dev, struct drm_agp_info *info);
int drm_agp_info_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
int drm_agp_alloc(struct drm_device *dev, struct drm_agp_buffer *request);
int drm_agp_alloc_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int drm_agp_free(struct drm_device *dev, struct drm_agp_buffer *request);
int drm_agp_free_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
int drm_agp_unbind(struct drm_device *dev, struct drm_agp_binding *request);
int drm_agp_unbind_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int drm_agp_bind(struct drm_device *dev, struct drm_agp_binding *request);
int drm_agp_bind_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);

#else /* CONFIG_AGP */

static inline void drm_free_agp(struct agp_memory * handle, int pages)
{
}

static inline int drm_bind_agp(struct agp_memory * handle, unsigned int start)
{
	return -ENODEV;
}

static inline int drm_unbind_agp(struct agp_memory * handle)
{
	return -ENODEV;
}

static inline struct agp_memory *drm_agp_bind_pages(struct drm_device *dev,
					      struct page **pages,
					      unsigned long num_pages,
					      uint32_t gtt_offset,
					      uint32_t type)
{
	return NULL;
}

static inline struct drm_agp_head *drm_agp_init(struct drm_device *dev)
{
	return NULL;
}

static inline void drm_agp_clear(struct drm_device *dev)
{
}

static inline int drm_agp_acquire(struct drm_device *dev)
{
	return -ENODEV;
}

static inline int drm_agp_release(struct drm_device *dev)
{
	return -ENODEV;
}

static inline int drm_agp_enable(struct drm_device *dev,
				 struct drm_agp_mode mode)
{
	return -ENODEV;
}

static inline int drm_agp_info(struct drm_device *dev,
			       struct drm_agp_info *info)
{
	return -ENODEV;
}

static inline int drm_agp_alloc(struct drm_device *dev,
				struct drm_agp_buffer *request)
{
	return -ENODEV;
}

static inline int drm_agp_free(struct drm_device *dev,
			       struct drm_agp_buffer *request)
{
	return -ENODEV;
}

static inline int drm_agp_unbind(struct drm_device *dev,
				 struct drm_agp_binding *request)
{
	return -ENODEV;
}

static inline int drm_agp_bind(struct drm_device *dev,
			       struct drm_agp_binding *request)
{
	return -ENODEV;
}

#endif /* CONFIG_AGP */

#endif /* _DRM_AGPSUPPORT_H_ */
