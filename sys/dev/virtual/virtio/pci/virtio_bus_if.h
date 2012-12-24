/*
 * This file is produced automatically.
 * Do not modify anything in here by hand.
 *
 * Created from source file
 *   @/dev/virtio/virtio_bus_if.m
 * with
 *   makeobjops.awk
 *
 * See the source file for legal information
 */

#ifndef _virtio_bus_if_h_
#define _virtio_bus_if_h_


struct vq_alloc_info;

extern struct kobjop_desc virtio_bus_negotiate_features_desc;
typedef uint64_t virtio_bus_negotiate_features_t(device_t dev,
                                                 uint64_t child_features);
static __inline uint64_t VIRTIO_BUS_NEGOTIATE_FEATURES(device_t dev,
                                                       uint64_t child_features)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_bus_negotiate_features);
	return ((virtio_bus_negotiate_features_t *) _m)(dev, child_features);
}

extern struct kobjop_desc virtio_bus_with_feature_desc;
typedef int virtio_bus_with_feature_t(device_t dev, uint64_t feature);
static __inline int VIRTIO_BUS_WITH_FEATURE(device_t dev, uint64_t feature)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_bus_with_feature);
	return ((virtio_bus_with_feature_t *) _m)(dev, feature);
}

extern struct kobjop_desc virtio_bus_alloc_virtqueues_desc;
typedef int virtio_bus_alloc_virtqueues_t(device_t dev, int flags, int nvqs,
                                          struct vq_alloc_info *info);
static __inline int VIRTIO_BUS_ALLOC_VIRTQUEUES(device_t dev, int flags,
                                                int nvqs,
                                                struct vq_alloc_info *info)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_bus_alloc_virtqueues);
	return ((virtio_bus_alloc_virtqueues_t *) _m)(dev, flags, nvqs, info);
}


#define VIRTIO_ALLOC_VQS_DISABLE_MSIX 0x1

extern struct kobjop_desc virtio_bus_setup_intr_desc;
typedef int virtio_bus_setup_intr_t(device_t dev);
static __inline int VIRTIO_BUS_SETUP_INTR(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_bus_setup_intr);
	return ((virtio_bus_setup_intr_t *) _m)(dev);
}

extern struct kobjop_desc virtio_bus_stop_desc;
typedef void virtio_bus_stop_t(device_t dev);
static __inline void VIRTIO_BUS_STOP(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_bus_stop);
	((virtio_bus_stop_t *) _m)(dev);
}

extern struct kobjop_desc virtio_bus_reinit_desc;
typedef int virtio_bus_reinit_t(device_t dev, uint64_t features);
static __inline int VIRTIO_BUS_REINIT(device_t dev, uint64_t features)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_bus_reinit);
	return ((virtio_bus_reinit_t *) _m)(dev, features);
}

extern struct kobjop_desc virtio_bus_reinit_complete_desc;
typedef void virtio_bus_reinit_complete_t(device_t dev);
static __inline void VIRTIO_BUS_REINIT_COMPLETE(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_bus_reinit_complete);
	((virtio_bus_reinit_complete_t *) _m)(dev);
}

extern struct kobjop_desc virtio_bus_notify_vq_desc;
typedef void virtio_bus_notify_vq_t(device_t dev, uint16_t queue);
static __inline void VIRTIO_BUS_NOTIFY_VQ(device_t dev, uint16_t queue)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_bus_notify_vq);
	((virtio_bus_notify_vq_t *) _m)(dev, queue);
}

extern struct kobjop_desc virtio_bus_read_device_config_desc;
typedef void virtio_bus_read_device_config_t(device_t dev, bus_size_t offset,
                                             void *dst, int len);
static __inline void VIRTIO_BUS_READ_DEVICE_CONFIG(device_t dev,
                                                   bus_size_t offset, void *dst,
                                                   int len)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_bus_read_device_config);
	((virtio_bus_read_device_config_t *) _m)(dev, offset, dst, len);
}

extern struct kobjop_desc virtio_bus_write_device_config_desc;
typedef void virtio_bus_write_device_config_t(device_t dev, bus_size_t offset,
                                              void *src, int len);
static __inline void VIRTIO_BUS_WRITE_DEVICE_CONFIG(device_t dev,
                                                    bus_size_t offset,
                                                    void *src, int len)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_bus_write_device_config);
	((virtio_bus_write_device_config_t *) _m)(dev, offset, src, len);
}

#endif /* _virtio_bus_if_h_ */
