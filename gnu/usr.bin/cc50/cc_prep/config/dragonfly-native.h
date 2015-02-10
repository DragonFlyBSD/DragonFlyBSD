#undef  PREFIX
#define PREFIX  PREFIX1
#ifndef PREFIX2
#define PREFIX2	PREFIX1
#endif

#define LINK_LIBGCC_SPEC ""
#define LIBGCC_SPEC \
"%{static|static-libgcc:-lgcc -lgcc_eh} \
 %{!static: \
  %{!static-libgcc: \
   %{!shared-libgcc:-lgcc --as-needed -lgcc_pic --no-as-needed} \
   %{shared-libgcc:-lgcc_pic \
    %{!shared: -lgcc} \
   } \
  } \
 }"

#undef	LINK_SPEC
#define LINK_SPEC DFBSD_LINK_SPEC \
"%{pg: -L"PREFIX2"/lib/gcc"GCCSHORTVER"/profile \
  %{!static: -rpath /usr/lib/gcc"GCCSHORTVER"/profile} \
 } \
 -L"PREFIX2"/lib/gcc"GCCSHORTVER" \
 %{!static: -rpath /usr/lib/gcc"GCCSHORTVER"} \
 %{pg: \
  %{!nostdlib: \
   %{!nodefaultlibs: -L"PREFIX2"/lib/profile \
    %{!static: -rpath /usr/lib/profile} \
   } \
  } \
 }"

#define NATIVE_SYSTEM_HEADER_DIR	PREFIX2"/include"
#define STD_EXEC_PATH			PREFIX1"/libexec/gcc"GCCSHORTVER
#define STANDARD_EXEC_PREFIX		STD_EXEC_PATH"/"
#define STANDARD_LIBEXEC_PREFIX 	STANDARD_EXEC_PREFIX
#define STANDARD_BINDIR_PREFIX		STANDARD_EXEC_PREFIX
#define STANDARD_STARTFILE_PREFIX	STANDARD_EXEC_PREFIX
#define STANDARD_STARTFILE_PREFIX_1	""
#define STANDARD_STARTFILE_PREFIX_2	""
#define MD_EXEC_PREFIX			""
#define MD_STARTFILE_PREFIX		""
#define MD_STARTFILE_PREFIX_1		""
#define TOOLDIR_BASE_PREFIX		"./"

#define STARTFILE_PREFIX_SPEC		PREFIX2"/lib/gcc"GCCSHORTVER"/ "PREFIX2"/lib/"

#define GPLUSPLUS_INCLUDE_DIR		PREFIX2"/include/c++/"GCCPOINTVER
#define GPLUSPLUS_INCLUDE_DIR_ADD_SYSROOT 0
#undef	GPLUSPLUS_TOOL_INCLUDE_DIR
#define	GPLUSPLUS_BACKWARD_INCLUDE_DIR	PREFIX2"/include/c++/"GCCPOINTVER"/backward"
#undef	LOCAL_INCLUDE_DIR
#define	GCC_INCLUDE_DIR			PREFIX2"/libdata/gcc"GCCSHORTVER
#undef	FIXED_INCLUDE_DIR
#undef	CROSS_INCLUDE_DIR
#undef	TOOL_INCLUDE_DIR
