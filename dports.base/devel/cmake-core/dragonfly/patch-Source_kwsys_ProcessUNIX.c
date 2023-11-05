--- Source/kwsys/ProcessUNIX.c.orig	2018-08-09 12:14:08 UTC
+++ Source/kwsys/ProcessUNIX.c
@@ -2467,7 +2467,8 @@ static pid_t kwsysProcessFork(kwsysProce
    corresponding parsing format string.  The parsing format should
    have two integers to store: the pid and then the ppid.  */
 #if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) ||       \
-  defined(__OpenBSD__) || defined(__GLIBC__) || defined(__GNU__)
+  defined(__OpenBSD__) || defined(__GLIBC__) || defined(__GNU__) || \
+  defined(__DragonFly__)
 #  define KWSYSPE_PS_COMMAND "ps axo pid,ppid"
 #  define KWSYSPE_PS_FORMAT "%d %d\n"
 #elif defined(__sun) && (defined(__SVR4) || defined(__svr4__)) /* Solaris */
