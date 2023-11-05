--- src/util.cc.orig	2013-09-12 02:15:28.000000000 +0000
+++ src/util.cc
@@ -36,7 +36,7 @@
 
 #include <vector>
 
-#if defined(__APPLE__) || defined(__FreeBSD__)
+#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__DragonFly__)
 #include <sys/sysctl.h>
 #elif defined(__SVR4) && defined(__sun)
 #include <unistd.h>
