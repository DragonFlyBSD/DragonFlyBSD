--- Source/Modules/CMakeBuildUtilities.cmake.orig	2022-11-30 13:57:03 UTC
+++ Source/Modules/CMakeBuildUtilities.cmake
@@ -151,7 +151,7 @@ else()
   endif()
   set(_CMAKE_USE_OPENSSL_DEFAULT OFF)
   if(NOT DEFINED CMAKE_USE_OPENSSL AND NOT WIN32 AND NOT APPLE
-      AND CMAKE_SYSTEM_NAME MATCHES "(Linux|FreeBSD)")
+      AND CMAKE_SYSTEM_NAME MATCHES "(Linux|FreeBSD|DragonFly)")
     set(_CMAKE_USE_OPENSSL_DEFAULT ON)
   endif()
   option(CMAKE_USE_OPENSSL "Use OpenSSL." ${_CMAKE_USE_OPENSSL_DEFAULT})
