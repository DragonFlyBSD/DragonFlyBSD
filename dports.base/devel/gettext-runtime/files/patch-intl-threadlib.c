--- intl/threadlib.c.orig	2013-03-07 08:44:37 UTC
+++ intl/threadlib.c
@@ -29,11 +29,10 @@
 
 # if PTHREAD_IN_USE_DETECTION_HARD
 
-/* The function to be executed by a dummy thread.  */
-static void *
-dummy_thread_func (void *arg)
+static pthread_once_t dummy_once_control = PTHREAD_ONCE_INIT;
+static void
+dummy_once_func (void)
 {
-  return arg;
 }
 
 int
@@ -44,19 +43,10 @@ glthread_in_use (void)
 
   if (!tested)
     {
-      pthread_t thread;
-
-      if (pthread_create (&thread, NULL, dummy_thread_func, NULL) != 0)
-        /* Thread creation failed.  */
+      if (pthread_once (&dummy_once_control, dummy_once_func) != 0)
         result = 0;
       else
-        {
-          /* Thread creation works.  */
-          void *retval;
-          if (pthread_join (thread, &retval) != 0)
-            abort ();
-          result = 1;
-        }
+        result = 1;
       tested = 1;
     }
   return result;
