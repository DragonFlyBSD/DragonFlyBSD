--- libpkg/lua_scripts.c.intermediate	2019-09-18 07:11:10 UTC
+++ libpkg/lua_scripts.c
@@ -30,6 +30,11 @@
 #include <sys/procctl.h>
 #endif
 
+#ifdef __DragonFly__
+#include <sys/types.h>
+#include <sys/wait.h>
+#endif
+
 #include <errno.h>
 #include <utstring.h>
 #include <lauxlib.h>
