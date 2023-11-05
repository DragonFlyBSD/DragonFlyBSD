--- libpkg/pkg_elf.c.orig	2020-04-29 07:28:26 UTC
+++ libpkg/pkg_elf.c
@@ -796,15 +796,15 @@ elf_note_analyse(Elf_Data *data, GElf_Eh
 	} else {
 		if (oi->osversion == 0)
 			oi->osversion = version;
-#ifdef __DragonFly__
+#if defined(__DragonFly__)
 		xasprintf(&oi->version, "%d.%d", version / 100000, (((version / 100 % 1000)+1)/2)*2);
-#endif
-#ifdef __NetBSD__
+#elif defined(__NetBSD__)
 		xasprintf(&oi->version, "%d", (version + 1000000) / 100000000);
-#endif
+#else
 		xasprintf(&oi->version_major, "%d", version / 100000);
 		xasprintf(&oi->version_minor, "%d", (version / 1000 % 100));
 		xasprintf(&oi->version, "%d", version / 100000);
+#endif
 	}
 
 	return (true);
