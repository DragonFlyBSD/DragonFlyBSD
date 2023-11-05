--- lib/getexecpath.c.orig	2010-11-18 22:47:35.000000000 +0200
+++ lib/getexecpath.c
@@ -27,7 +27,7 @@
 #define	SYMLINK_PATH	"/proc/self/path/a.out"	/* Solaris 10 -> ... */
 #endif
 
-#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
 #define	PATH_IMPL
 #define	METHOD_SYMLINK
 #define	SYMLINK_PATH	"/proc/curproc/file"	/* /proc may nor be mounted */
