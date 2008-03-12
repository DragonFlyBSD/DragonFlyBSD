--- posix.lua.orig	2003-06-18 05:47:44.000000000 -0700
+++ posix.lua	2005-03-28 20:13:37.000000000 -0800
@@ -2,9 +2,8 @@
 -- support code for posix library
 -- usage lua -lposix ...
 
-local function so(x)
-	local SOPATH= os.getenv"LUA_SOPATH" or "./"
-	assert(loadlib(SOPATH.."l"..x..".so","luaopen_"..x))()
-end
+module("posix")
 
-so"posix"
+require "lposix"
+
+return posix
