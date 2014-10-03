/*
 * SYS/BOOTMAJ.H
 *
 * Major device numbers as passed from the boot code to the main kernel
 * usually require translation due to the fact that we no longer have
 * block device.
 *
 * $DragonFly: src/sys/sys/bootmaj.h,v 1.1 2003/07/21 07:06:45 dillon Exp $
 */

#define WDMAJOR			0          
#define WFDMAJOR		1
#define FDMAJOR			2
#define DAMAJOR			4
#define SCSICDMAJOR		6
#define MCDMAJOR		7

#define WD_CDEV_MAJOR		3
#define WFD_CDEV_MAJOR		87
#define FD_CDEV_MAJOR		9
#define DA_CDEV_MAJOR		13
#define SCSICD_CDEV_MAJOR	15
#define MCD_CDEV_MAJOR		29

#define BOOTMAJOR_CONVARY	\
	WD_CDEV_MAJOR, WFD_CDEV_MAJOR, FD_CDEV_MAJOR, -1, DA_CDEV_MAJOR, \
	-1, SCSICD_CDEV_MAJOR, MCD_CDEV_MAJOR
