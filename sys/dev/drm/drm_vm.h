/*
 * $FreeBSD: src/sys/dev/drm/drm_vm.h,v 1.6.2.1 2003/04/26 07:05:29 anholt Exp $
 * $DragonFly: src/sys/dev/drm/Attic/drm_vm.h,v 1.4 2006/07/28 02:17:36 dillon Exp $
 */

#if defined(__FreeBSD__) && __FreeBSD_version >= 500102
static int DRM(dma_mmap)(dev_t kdev, vm_offset_t offset, vm_paddr_t *paddr, 
    int prot)
#elif defined(__DragonFly__)
static int DRM(dma_mmap)(struct dev_mmap_args *ap)
#elif defined(__FreeBSD__)
static int DRM(dma_mmap)(dev_t kdev, vm_offset_t offset, int prot)
#elif defined(__NetBSD__)
static paddr_t DRM(dma_mmap)(dev_t kdev, vm_offset_t offset, int prot)
#endif
{
#ifdef __DragonFly__
	dev_t kdev = ap->a_head.a_dev;
	vm_offset_t offset = ap->a_offset;
#endif
	DRM_DEVICE;
	drm_device_dma_t *dma	 = dev->dma;
	unsigned long	 physical;
	unsigned long	 page;

	if (!dma)		   return -1; /* Error */
	if (!dma->pagelist)	   return -1; /* Nothing allocated */

	page	 = offset >> PAGE_SHIFT;
	physical = dma->pagelist[page];

	DRM_DEBUG("0x%08lx (page %lu) => 0x%08lx\n", (long)offset, page, physical);
#if defined(__DragonFly__)
	ap->a_result = physical;
	return 0;
#elif defined(__FreeBSD__) && __FreeBSD_version >= 500102
	*paddr = physical;
	return 0;
#else
	return atop(physical);
#endif
}

#if defined(__FreeBSD__) && __FreeBSD_version >= 500102
int DRM(mmap)(dev_t kdev, vm_offset_t offset, vm_paddr_t *paddr, 
    int prot)
#elif defined(__DragonFly__)
int DRM(mmap)(struct dev_mmap_args *ap)
#elif defined(__NetBSD__)
paddr_t DRM(mmap)(dev_t kdev, off_t offset, int prot)
#endif
{
	dev_t kdev = ap->a_head.a_dev;
	DRM_DEVICE;
	drm_local_map_t *map	= NULL;
	drm_map_list_entry_t *listentry=NULL;
	drm_file_t *priv;

	priv = DRM(find_file_by_proc)(dev, DRM_CURPROC);
	if (!priv) {
		DRM_DEBUG("can't find authenticator\n");
		return EINVAL;
	}

	if (!priv->authenticated)
		return DRM_ERR(EACCES);

	if (dev->dma
	    && ap->a_offset >= 0
	    && ap->a_offset < ptoa(dev->dma->page_count))
#if defined(__FreeBSD__) && __FreeBSD_version >= 500102
		return DRM(dma_mmap)(kdev, offset, paddr, prot);
#elif defined(__DragonFly__)
		return DRM(dma_mmap)(ap);
#else
		return DRM(dma_mmap)(kdev, ap->a_offset, ap->a_nprot);
#endif

				/* A sequential search of a linked list is
				   fine here because: 1) there will only be
				   about 5-10 entries in the list and, 2) a
				   DRI client only has to do this mapping
				   once, so it doesn't have to be optimized
				   for performance, even if the list was a
				   bit longer. */
	TAILQ_FOREACH(listentry, dev->maplist, link) {
		map = listentry->map;
/*		DRM_DEBUG("considering 0x%x..0x%x\n", map->offset, map->offset + map->size - 1);*/
		if (ap->a_offset >= map->offset
		    && ap->a_offset < map->offset + map->size) break;
	}
	
	if (!listentry) {
		DRM_DEBUG("can't find map\n");
		return EINVAL;
	}
	if (((map->flags&_DRM_RESTRICTED) && DRM_SUSER(DRM_CURPROC))) {
		DRM_DEBUG("restricted map\n");
		return EINVAL;
	}

	switch (map->type) {
	case _DRM_FRAME_BUFFER:
	case _DRM_REGISTERS:
	case _DRM_AGP:
#if defined(__FreeBSD__) && __FreeBSD_version >= 500102
		*paddr = ap->a_offset;
		return 0;
#elif defined(__DragonFly__)
		ap->a_result = atop(ap->a_offset);
		return 0;
#else
		return atop(ap->a_offset);
#endif
	case _DRM_SCATTER_GATHER:
	case _DRM_SHM:
#if defined(__FreeBSD__) && __FreeBSD_version >= 500102
		*paddr = vtophys(ap->a_offset);
		return 0;
#elif defined(__DragonFly__)
		ap->a_result = vtophys(ap->a_offset);
		return 0;
#else
		return atop(vtophys(ap->a_offset));
#endif
	default:
		return EINVAL;	/* This should never happen. */
	}
	DRM_DEBUG("bailing out\n");
	
	return EINVAL;
}

