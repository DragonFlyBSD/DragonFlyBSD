/*
 * This file is produced automatically.
 * Do not modify anything in here by hand.
 *
 * Created from source file
 *   @/dev/virtio/virtio_if.m
 * with
 *   makeobjops.awk
 *
 * See the source file for legal information
 */

#ifndef _virtio_if_h_
#define _virtio_if_h_

extern struct kobjop_desc virtio_config_change_desc;
typedef int virtio_config_change_t(device_t dev);
static __inline int VIRTIO_CONFIG_CHANGE(device_t dev)
{
	kobjop_t _m;
	KOBJOPLOOKUP(((kobj_t)dev)->ops, virtio_config_change);
	return ((virtio_config_change_t *) _m)(dev);
}

#endif /* _virtio_if_h_ */
