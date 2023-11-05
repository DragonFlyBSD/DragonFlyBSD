--- auto.def.orig	2021-02-18 10:07:35.000000000 -0800
+++ auto.def	2021-04-01 10:33:23.286115000 -0700
@@ -96,9 +96,9 @@
 }
 
 define ZSTDLIB ""
-cc-with { -libs { -lprivatezstd }} {
+cc-with { -libs { -lprivate_zstd }} {
 	if {[cc-check-functions ZSTD_versionNumber]} {
-		define ZSTDLIB "-lprivatezstd"
+		define ZSTDLIB "-lprivate_zstd"
 	}
 }
 
