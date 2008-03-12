--- compat-5.1.c.orig	Mon Apr  4 17:35:33 2005
+++ compat-5.1.c	Mon Apr  4 17:35:51 2005
@@ -1,7 +1,7 @@
 #include <stdio.h>
 #include <string.h>
-#include "lua.h"
-#include "lauxlib.h"
+#include "lua50/lua.h"
+#include "lua50/lauxlib.h"
 #include "compat-5.1.h"
 
 static void getfield(lua_State *L, const char *name) {
