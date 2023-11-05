--- Modules/GNUInstallDirs.cmake.orig	2022-07-29 00:55:12 UTC
+++ Modules/GNUInstallDirs.cmake
@@ -330,7 +330,7 @@ else()
     "Info documentation (DATAROOTDIR/info)")
 endif()
 
-if(CMAKE_SYSTEM_NAME MATCHES "^(([^k].*)?BSD|DragonFly)$" AND NOT CMAKE_SYSTEM_NAME MATCHES "^(FreeBSD)$")
+if(CMAKE_SYSTEM_NAME MATCHES "^(([^k].*)?BSD)$" AND NOT CMAKE_SYSTEM_NAME MATCHES "^(FreeBSD)$")
   _GNUInstallDirs_cache_path_fallback(CMAKE_INSTALL_MANDIR "man"
     "Man documentation (man)")
 else()
