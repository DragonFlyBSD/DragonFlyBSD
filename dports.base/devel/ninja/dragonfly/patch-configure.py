--- configure.py.intermediate	2014-12-12 13:05:20 UTC
+++ configure.py
@@ -41,6 +41,8 @@ class Platform(object):
         self._platform = sys.platform
         if self._platform.startswith('linux'):
             self._platform = 'linux'
+        elif self._platform.startswith('dragonfly'):
+            self._platform = 'freebsd'
         elif self._platform.startswith('freebsd'):
             self._platform = 'freebsd'
         elif self._platform.startswith('gnukfreebsd'):
