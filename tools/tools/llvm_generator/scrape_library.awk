# must set SRCDIR externally

BEGIN {
  scanning=0;
  headers=0;
  depends=0;
  linklibs=0;
  x86=0;
  srcs=0;
  print "# Automatically generated.  Do not edit!";
  print
}

{
  if (scanning) {
     keep_going = (NF > 0);
     if (!keep_going) {
        linklibs = 0;
     }
     if (keep_going && ($1 == "#")) {
        keep_going = 0;
     }
     if (keep_going && ($1 == ")")) {
        keep_going = 0;
        scanning = 0;
     }
     if (keep_going && ($1 == "ADDITIONAL_HEADER_DIRS")) {
        keep_going = 0;
        headers = 1;
        print "";
     }
     if (keep_going && ($1 == "ADDITIONAL_HEADERS")) {
        keep_going = 0;
        headers = 1;
        print "";
     }
     if (keep_going && ($1 == "DEPENDS")) {
        keep_going = 0;
        headers = 0;
        depends = 1;
     }
     if (keep_going && ($1 == "LINK_LIBS")) {
        keep_going = 0;
        headers = 0;
        linklibs = 1;
     }
     if (keep_going && headers) {
        if (substr($1,2,24) == "{LLVM_MAIN_INCLUDE_DIR}/") {
           srcdir=substr($1,26)
           print "HDRDIR=\t" srcdir
        }
        keep_going = 0;
     }
     if (keep_going && depends) {
        keep_going = 0;
     }
     if (keep_going && linklibs) {
        keep_going = 0;
     }
     if (keep_going) {
        if (srcs) {
           print "\t" $1 " \\";
        } else {
           srcs=1;
           print "SRCS=\t" $1 " \\";
        }
     }
  } else if (substr($1,1,18) ==  "add_clang_library(") {
       scanning=1;
       libname=substr($1,19)
       print "LIB=\t" libname "\n"
  } else if (substr($1,1,17) ==  "add_llvm_library(") {
       scanning=1;
       libname=substr($1,18)
       print "LIB=\t" libname "\n"
  } else if (substr($1,1,16) ==  "add_llvm_target(") {
       libname=substr($1,17)
       if (x86 == 1) {
          # X86 code generator
          print "\nLIB=\tLLVM" libname
       } else {
          # AArch64, ARM, AMDGPU, Mips, PowerPC code generators
          scanning=1;
          print "LIB=\tLLVM" libname "\n"
       }
  } else if (substr($1,1,11) ==  "set(sources") {
       scanning=1
       x86=1;
  }
}

END {
  print "\nSRCDIR= " SRCDIR
}
