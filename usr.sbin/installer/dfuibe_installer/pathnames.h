/*
 * Pathnames for DragonFly installer backend.
 * $Id: pathnames.h,v 1.12 2004/10/12 06:31:32 den Exp $
 */

#ifndef __PATHNAMES_H_
#define __PATHNAMES_H_

/*
 * Default location where the files to copy onto the disk
 * reside and generally where the installation is mounted.
 * Normally this will be "/", when booting from a live CD,
 * but for testing purposes we can set it to (say) "/cdrom/",
 * so that we needn't boot from a CD in development.
 * Note that this must include the trailing slash.
 */
#ifndef DEFAULT_OS_ROOT
#define	DEFAULT_OS_ROOT		"/"
#endif

/*
 * Default directory in which temporary files are placed.
 * /tmp/ is generally MFS mounted when booting from a live CD,
 * so we can use it both in development and in production.
 * Note that this must include the trailing slash.
 * Note that this is NOT relative to the source file root dir.
 */
#ifndef DEFAULT_INSTALLER_TEMP
#define	DEFAULT_INSTALLER_TEMP	"/tmp/"
#endif

/*
 * Directory from which to copy 'pristine' files over the
 * installed files after the main installation phase is done.
 * This is actually something of a misnomer now, as these
 * files would typically be 3rd-party customizations.
 */
#ifndef PRISTINE_DIR
#define	PRISTINE_DIR	".pristine"
#endif

#ifndef PRISTINE_DIR_ROOT
#define PRISTINE_DIR_ROOT	".pristineroot"
#endif

#endif /* !__PATHNAMES_H_ */
