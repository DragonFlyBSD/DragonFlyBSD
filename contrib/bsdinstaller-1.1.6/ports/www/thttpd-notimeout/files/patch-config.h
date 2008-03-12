--- config.h.orig	2004-06-06 18:09:22.000000000 -0700
+++ config.h	2004-06-06 18:09:58.000000000 -0700
@@ -75,7 +75,7 @@
 ** or whatever.  If you don't want any limit, comment this out, but that's
 ** probably a really bad idea.
 */
-#define CGI_TIMELIMIT 30
+/* #define CGI_TIMELIMIT 30 */
 
 /* CONFIGURE: How many seconds to allow for reading the initial request
 ** on a new connection.
