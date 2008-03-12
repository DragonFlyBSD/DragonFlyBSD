--- lposix.c.orig	Thu Nov  6 03:23:48 2003
+++ lposix.c	Mon Apr  4 17:30:55 2005
@@ -9,6 +9,7 @@
 #include <errno.h>
 #include <fcntl.h>
 #include <grp.h>
+#include <math.h>
 #include <pwd.h>
 #include <signal.h>
 #include <stdio.h>
@@ -24,10 +25,10 @@
 #include <utime.h>
 
 #define MYNAME		"posix"
-#define MYVERSION	MYNAME " library for " LUA_VERSION " / Nov 2003"
+#define MYVERSION	"2005.0327"
 
-#include "lua.h"
-#include "lauxlib.h"
+#include "lua50/lua.h"
+#include "lua50/lauxlib.h"
 
 #ifndef MYBUFSIZ
 #define MYBUFSIZ 512
@@ -112,6 +113,20 @@
 		return pusherror(L, info);
 }
 
+/*
+ * This function is adapted from liolib.c: push a FILE * onto the
+ * Lua stack as a file object that Lua's file module understands.
+ */
+static void pushfileptr(lua_State *L, FILE *f)
+{
+	FILE **pf;
+
+	pf = (FILE **)lua_newuserdata(L, sizeof(FILE *));
+	*pf = f;  
+	luaL_getmetatable(L, "FILE*");
+	lua_setmetatable(L, -2);
+}
+
 static void badoption(lua_State *L, int i, const char *what, int option)
 {
 	luaL_argerror(L, 2,
@@ -312,6 +327,33 @@
 }
 
 
+static int Pmkstemp(lua_State *L)		/** mkstemp(template) */
+{
+	char *tpl;
+	int fd;
+	FILE *f;
+
+	if ((tpl = strdup(luaL_checkstring(L, 1))) == NULL) {
+		lua_pushnil(L);
+		lua_pushnumber(L, ENOMEM);
+		return(2);
+	}
+	fd = mkstemp(tpl);
+	f = fdopen(fd, "w+");
+	if (f == NULL) {
+		lua_pushnil(L);
+		lua_pushnumber(L, errno);
+		free(tpl);
+		return(1);
+	}
+
+	pushfileptr(L, f);
+	lua_pushstring(L, tpl);
+	free(tpl);
+	return(2);
+}
+
+
 static int Pexec(lua_State *L)			/** exec(path,[args]) */
 {
 	const char *path = luaL_checkstring(L, 1);
@@ -355,6 +397,40 @@
 }
 
 
+static int Pnanosleep(lua_State *L)		/** nanosleep(secs,[nanosecs]) */
+{
+	double sec, nsec;
+	struct timespec ts;
+
+	sec = lua_tonumber(L, 1);
+	nsec = lua_tonumber(L, 2);
+
+	/*
+	 * Any fractional part of the seconds value should
+	 * slide over into the nanoseconds value.
+	 */
+	nsec += (sec - floor(sec)) * 1000000000.0;
+
+	/*
+	 * Don't allow overflow.
+	 */
+	if (sec > 1000000000.0) {
+		sec = 1000000000.0;
+	}
+	while (nsec > 1000000000.0) {
+		nsec -= 1000000000.0;
+		sec += 1.0;
+	}
+
+	ts.tv_sec = sec;
+	ts.tv_nsec = nsec;
+
+	lua_pushnumber(L, nanosleep(&ts, NULL));
+
+	return 1;
+}
+
+
 static int Pputenv(lua_State *L)		/** putenv(string) */
 {
 	size_t l;
@@ -777,6 +853,8 @@
 	{"link",		Plink},
 	{"mkdir",		Pmkdir},
 	{"mkfifo",		Pmkfifo},
+	{"mkstemp",		Pmkstemp},
+	{"nanosleep",		Pnanosleep},
 	{"pathconf",		Ppathconf},
 	{"putenv",		Pputenv},
 	{"readlink",		Preadlink},
@@ -809,4 +887,9 @@
 	lua_pushliteral(L,MYVERSION);
 	lua_settable(L,-3);
 	return 1;
+}
+
+LUALIB_API int luaopen_lposix (lua_State *L)
+{
+	return luaopen_posix(L);
 }
