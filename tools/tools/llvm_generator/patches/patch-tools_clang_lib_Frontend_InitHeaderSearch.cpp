--- tools/clang/lib/Frontend/InitHeaderSearch.cpp.orig	2015-12-30 03:40:23 UTC
+++ tools/clang/lib/Frontend/InitHeaderSearch.cpp
@@ -211,6 +211,7 @@ void InitHeaderSearch::AddDefaultCInclud
   if (HSOpts.UseStandardSystemIncludes) {
     switch (os) {
     case llvm::Triple::CloudABI:
+    case llvm::Triple::DragonFly:
     case llvm::Triple::FreeBSD:
     case llvm::Triple::NetBSD:
     case llvm::Triple::OpenBSD:
@@ -231,7 +232,7 @@ void InitHeaderSearch::AddDefaultCInclud
 
   // Builtin includes use #include_next directives and should be positioned
   // just prior C include dirs.
-  if (HSOpts.UseBuiltinIncludes) {
+  if (0) { //(HSOpts.UseBuiltinIncludes) {
     // Ignore the sys root, we *always* look for clang headers relative to
     // supplied path.
     SmallString<128> P = StringRef(HSOpts.ResourceDir);
@@ -255,6 +256,11 @@ void InitHeaderSearch::AddDefaultCInclud
   }
 
   switch (os) {
+    case llvm::Triple::DragonFly: {
+    AddPath(DF_CLANG_HEADERS, ExternCSystem, false);
+    break;
+  }
+    
   case llvm::Triple::Linux:
     llvm_unreachable("Include management is handled in the driver.");
 
