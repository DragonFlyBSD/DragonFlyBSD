--- lib/libzstd.mk.orig	2022-10-14 10:33:23.696276000 +0200
+++ lib/libzstd.mk	2022-10-14 10:34:18.455035000 +0200
@@ -58,7 +58,7 @@
 LIBVER_MINOR := $(shell echo $(LIBVER_MINOR_SCRIPT))
 LIBVER_PATCH := $(shell echo $(LIBVER_PATCH_SCRIPT))
 LIBVER := $(shell echo $(LIBVER_SCRIPT))
-CCVER := $(shell $(CC) --version)
+CCVER := $($(CC) --version)
 ZSTD_VERSION?= $(LIBVER)
 
 # ZSTD_LIB_MINIFY is a helper variable that
@@ -187,6 +187,8 @@
   HASH ?= md5 -n
 else ifeq ($(UNAME), OpenBSD)
   HASH ?= md5
+else ifeq ($(UNAME), DragonFly)
+  HASH ?= md5
 endif
 HASH ?= md5sum
 
